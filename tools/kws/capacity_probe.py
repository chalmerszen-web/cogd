"""TRAIN-only small-set fitting diagnosis; never exports a deployable candidate."""
import hashlib
import json
from pathlib import Path
import sys
import time

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from model import Model
from train import masked_bce
from evaluate import metrics


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fit(data, channels, out):
    seed = 20261001
    torch.manual_seed(seed)
    rng = np.random.default_rng(seed)
    net = Model(channels)
    optimizer = torch.optim.AdamW(net.parameters(), lr=.001, weight_decay=.0001)
    x = torch.from_numpy(data['x'].astype(np.float32).transpose(0, 2, 1) / 32)
    y = torch.from_numpy(data['y'][:, None, :])
    groups = [np.flatnonzero((data['label'] == 1) & (data['language'] == lang)) for lang in ('zh', 'yue')]
    groups.append(np.flatnonzero(data['label'] == 0))
    history = []
    started = time.monotonic()
    # Fixed budget and final checkpoint; no validation or best-checkpoint search.
    for step in range(1, 1501):
        net.train()
        choices = rng.choice(3, 32, p=[.25, .25, .5])
        indices = np.array([rng.choice(groups[g]) for g in choices])
        loss = masked_bce(net(x[indices])[:, :, 128:], y[indices, :, 128:])
        optimizer.zero_grad()
        loss.backward()
        torch.nn.utils.clip_grad_norm_(net.parameters(), 5.)
        optimizer.step()
        if step % 250 == 0:
            row = dict(step=step, loss=float(loss.detach()), seconds=time.monotonic() - started)
            history.append(row)
            print(json.dumps(dict(channels=channels, **row)), flush=True)
    net.eval()
    with torch.no_grad():
        scores = np.concatenate([net(x[i:i + 32])[:, 0].numpy() * 256 for i in range(0, len(x), 32)])
    # Original G's independently frozen floating threshold: no threshold fitting.
    threshold = 295
    measured = metrics(scores, data, threshold)
    passed = (all(m['hits'] == m['total'] for m in measured['languages'].values())
              and measured['negative_triggered_clips'] == 0 and measured['premature_events'] == 0)
    checkpoint = out / f'c{channels}-diagnostic-only.pt'
    torch.save(dict(state_dict=net.state_dict(), channels=channels, seed=seed, steps=1500,
                    diagnostic_only=True, deployable=False), checkpoint)
    np.savez_compressed(out / f'c{channels}-scores.npz', scores=scores, clip_id=data['clip_id'])
    report = dict(channels=channels, parameters=sum(p.numel() for p in net.parameters()),
                  steps=1500, seed=seed, seconds=time.monotonic() - started, history=history,
                  metrics=measured, fits_small_train_set=passed, checkpoint_sha256=sha(checkpoint))
    (out / f'c{channels}.json').write_text(json.dumps(report, indent=2))
    return report


def main():
    base = ROOT / 'artifacts/kws-phase5'
    source = base / 'pcen-g-features/train.npz'
    old_selection = base / 'pcen-g-int8/diagnosis-train-selection.json'
    frozen_threshold = base / 'pcen-g-int8/threshold.json'
    assert json.loads(frozen_threshold.read_text())['floating'] == 295
    data = np.load(source, allow_pickle=False)
    indices = np.array(json.loads(old_selection.read_text())['indices'])
    assert len(indices) == 268 and data['device_domain'][indices].all() and (data['variant'][indices] == 0).all()
    selected = {k: data[k][indices] for k in data.files if k != 'device_domain'}
    assert selected['clip_id'].tolist() == json.loads(old_selection.read_text())['clip_ids']
    out = base / 'capacity-probe'
    out.mkdir(exist_ok=False)
    sources = [Path(__file__), ROOT / 'training/kws/model.py', ROOT / 'training/kws/train.py', ROOT / 'training/kws/evaluate.py']
    frozen = {str(p.relative_to(ROOT)): sha(p) for p in sources + [source, old_selection, frozen_threshold]}
    (out / 'input-freeze.json').write_text(json.dumps(frozen, indent=2))
    for p in sources:
        (out / p.name).write_bytes(p.read_bytes())
    torch.set_num_threads(4)
    torch.use_deterministic_algorithms(True)
    reports = [fit(selected, 24, out)]
    if not reports[0]['fits_small_train_set']:
        reports.append(fit(selected, 48, out))
    assert frozen == {str(p.relative_to(ROOT)): sha(p) for p in sources + [source, old_selection, frozen_threshold]}
    report = dict(complete=True, results=reports, validation_read=False, test_read=False,
                  trained_on='268 unattenuated captured TRAIN examples only',
                  limitation='In-sample diagnostic. Success does not prove generalization; failure does not prove insufficient capacity. No C3 resource/quality claim.',
                  deployable=False, flashed=False)
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(dict(complete=True, widths=[r['channels'] for r in reports],
                         fits=[r['fits_small_train_set'] for r in reports])), flush=True)


if __name__ == '__main__':
    main()
