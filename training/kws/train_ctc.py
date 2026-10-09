"""Bounded, reproducible full-TRAIN fit of the continuous C11 token model.

The caller freezes a plan and input hashes before running this file. This
trainer never reads DEV/TEST, selects checkpoints, exports failed weights,
or contacts the device. Phonetic mode assigns unknown input a catch-all OTHER;
whole-event mode assigns no wake event. Neither claims a phonetic transcript.
"""
from pathlib import Path
import hashlib
import json
import time

import numpy as np
import torch

from ctc_data import TOKENS
from ctc_model import CTCModel
from ctc_sequence import sequence_loss
from ctc_timed import timed_sequence_loss, audited_final_windows


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n',
                          encoding='utf-8')


def load_inputs(root, plan):
    """Verify complete input files, then load the immutable base features."""
    for entry in plan['inputs'].values():
        if sha256(root / entry['path']) != entry['sha256']:
            raise ValueError('Input changed: ' + entry['path'])
    with np.load(root / plan['inputs']['train']['path'], allow_pickle=False) as f:
        data = {key: f[key] for key in f.files}
    with np.load(root / plan['inputs']['targets']['path'], allow_pickle=False) as f:
        labels = {key: f[key] for key in f.files}
    with np.load(root / plan['inputs']['blank']['path'], allow_pickle=False) as f:
        blank = f['x']
    x = data['x']
    if (x.shape != (15791, 256, 40) or x.dtype != np.int8 or
            blank.shape != (256, 40) or blank.dtype != np.int8 or
            not np.all(blank == blank[0]) or
            not np.array_equal(data['label'], labels['class_id'] == 0)):
        raise ValueError('Changed base, normalization, or positive truth')
    for key in ('language', 'source_group', 'clip_id', 'sampling_group', 'device_domain'):
        np.testing.assert_array_equal(data[key], labels[key])
    return data, labels, blank


def sampling_pools(data, labels):
    """Stratify by declared source, never by candidate scores."""
    pools = []
    for language in ('zh', 'yue'):
        for word in range(8):
            selected = np.flatnonzero((data['language'] == language) &
                                      (labels['class_id'] == word))
            groups = [selected[data['source_group'][selected] == group]
                      for group in sorted(set(data['source_group'][selected]))]
            if not groups or any(not len(group) for group in groups):
                raise ValueError('Missing complete-word source group')
            pools.append((4 if word == 0 else 1, groups))
    for name, count in (('natural_negative', 3), ('hard_negative', 3),
                        ('paired_negative', 2)):
        selected = np.flatnonzero((data['sampling_group'] == name) &
                                  (labels['class_id'] < 0))
        groups = [selected[data['source_group'][selected] == group]
                  for group in sorted(set(data['source_group'][selected]))]
        if not groups or any(not len(group) for group in groups):
            raise ValueError('Missing unknown-negative source group')
        pools.append((count, groups))
    if sum(count for count, _ in pools) != 30:
        raise ValueError('Unexpected frozen batch composition')
    return pools


def draw_indices(pools, rng):
    indices = []
    for count, groups in pools:
        for _ in range(count):
            group = groups[int(rng.integers(len(groups)))]
            indices.append(int(group[int(rng.integers(len(group)))]))
    return np.asarray(indices, dtype=np.int64)


def fit(root, out, plan):
    """Exactly one fresh fit and only the frozen final checkpoint."""
    started = time.monotonic()
    data, labels, blank = load_inputs(root, plan)
    cfg = plan['model']
    torch.set_num_threads(cfg['CPU_threads'])
    torch.manual_seed(cfg['seed'])
    torch.use_deterministic_algorithms(True)
    whole_event = cfg.get('whole_event', False)
    joint = cfg.get('phonetic_regularizer', False)
    if joint and not whole_event:
        raise ValueError('Phonetic regularizer requires the whole-event contract')
    if whole_event:
        from word_event import WordEventModel, event_targets, event_loss, TOKENS as event_tokens
    if joint:
        from joint_event import JointEventModel, joint_loss
    network = JointEventModel() if joint else WordEventModel() if whole_event else CTCModel()
    if sum(p.numel() for p in network.parameters()) != cfg['parameters']:
        raise ValueError('Model parameter contract changed')
    optimizer = torch.optim.Adam(network.parameters(), lr=cfg['learning_rate'])
    rng = np.random.default_rng(cfg['seed'])
    pools = sampling_pools(data, labels)
    features = torch.from_numpy(data['x'].astype(np.float32).transpose(0, 2, 1) / 32)
    if whole_event:
        raw_targets, raw_lengths = event_targets(data['label'].astype(np.int64))
        targets, lengths = torch.from_numpy(raw_targets), torch.from_numpy(raw_lengths)
    else:
        targets = torch.from_numpy(labels['targets'])
        lengths = torch.from_numpy(labels['lengths'])
    quiet = torch.from_numpy(blank.astype(np.float32).T[None] / 32)
    prefix = quiet[:, :, :cfg['known_blank_frames']]
    quiet_batch = quiet.expand(cfg['blank_rows'], -1, -1)
    quiet_targets = torch.zeros((cfg['blank_rows'], targets.shape[1]), dtype=torch.int64)
    quiet_lengths = torch.zeros(cfg['blank_rows'], dtype=torch.int64)
    if joint:
        phone_targets = torch.from_numpy(labels['targets'])
        phone_lengths = torch.from_numpy(labels['lengths'])
        quiet_phones = torch.zeros((cfg['blank_rows'], 4), dtype=torch.int64)
    timed = whole_event or cfg.get('timing_supervision', False)
    if timed:
        windows = torch.from_numpy(audited_final_windows(data['event_start_accept'],
            data['event_end'], data['label'].astype(bool)))
        quiet_windows = torch.full((cfg['blank_rows'], 2), -1, dtype=torch.int64)
    visits = np.zeros(len(features), dtype=np.int32)
    history = []
    first_loss = None
    network.train()
    fitting = time.monotonic()
    for step in range(1, cfg['steps'] + 1):
        if time.monotonic() - fitting > plan['maximum_fit_seconds']:
            raise TimeoutError('Frozen fit budget exhausted; no restart allowed')
        indices = draw_indices(pools, rng)
        np.add.at(visits, indices, 1)
        suffix = torch.cat((features[indices], quiet_batch), 0)
        values = torch.cat((prefix.expand(len(suffix), -1, -1), suffix), -1)
        wanted = torch.cat((targets[indices], quiet_targets), 0)
        sizes = torch.cat((lengths[indices], quiet_lengths), 0)
        optimizer.zero_grad(set_to_none=True)
        logits = network(values)
        if joint:
            intervals = torch.cat((windows[indices], quiet_windows), 0)
            supervised = torch.cat((phone_targets[indices], quiet_phones), 0)
            supervised_lengths = torch.cat((phone_lengths[indices], quiet_lengths), 0)
            objective = joint_loss(logits, supervised, supervised_lengths,
                                   wanted, sizes, intervals, cfg['known_blank_frames'])
        elif whole_event:
            intervals = torch.cat((windows[indices], quiet_windows), 0)
            objective = event_loss(logits, wanted, sizes, intervals, cfg['known_blank_frames'])
        elif timed:
            intervals = torch.cat((windows[indices], quiet_windows), 0)
            objective = timed_sequence_loss(logits, wanted, sizes, intervals,
                                            cfg['known_blank_frames'])
        else:
            objective = sequence_loss(logits, wanted, sizes, cfg['known_blank_frames'])
        if not torch.isfinite(objective):
            raise ValueError('Nonfinite path objective')
        objective.backward()
        norm = torch.nn.utils.clip_grad_norm_(network.parameters(),
                                             cfg['gradient_clip'], error_if_nonfinite=True)
        optimizer.step()
        if first_loss is None:
            first_loss = float(objective.detach())
        if step % 500 == 0:
            entry = dict(step=step, loss=float(objective.detach()),
                         gradient_norm=float(norm),
                         seconds=round(time.monotonic() - fitting, 3))
            history.append(entry)
            save_json(out / 'history.json', history)
            print(json.dumps(entry), flush=True)
    network.eval()
    torch.save(dict(state_dict=network.state_dict(), step=cfg['steps'],
                    config=dict(**cfg, tokens=list(event_tokens if whole_event else TOKENS),
                                auxiliary_tokens=list(TOKENS) if joint else [],
                                objective=plan['objective'],
                                runtime_contract=plan['runtime_contract'],
                                candidate='only-final', input_hashes=plan['inputs'])),
               out / 'checkpoint.pt')
    np.save(out / 'visit-counts.npy', visits)
    report = dict(complete=True, fits=1, final_step=cfg['steps'],
                  parameters=cfg['parameters'], first_loss=first_loss,
                  final_batch_loss=history[-1]['loss'],
                  fit_seconds=round(time.monotonic() - fitting, 3),
                  total_seconds=round(time.monotonic() - started, 3),
                  rows_seen=int((visits > 0).sum()), total_rows=len(visits),
                  optimizer_draws=int(visits.sum()),
                  checkpoint_sha256=sha256(out / 'checkpoint.pt'),
                  TRAIN_only=True, calibrations=0, exports=0,
                  quality_passed=None, false_wake_fixed=False)
    save_json(out / 'fit-report.json', report)
    return network, data, labels, blank, report
