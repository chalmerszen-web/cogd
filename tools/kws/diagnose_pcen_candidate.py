"""Bounded, read-only diagnosis of failed G; never retune or evaluate TEST."""
import hashlib
import json
from pathlib import Path
import sys

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from evaluate import events, metrics, scores_for, threshold_q8


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compact(m):
    return dict(hits={k: v['hits'] for k, v in m['languages'].items()},
                totals={k: v['total'] for k, v in m['languages'].items()},
                negative_hits=m['negative_triggered_clips'],
                negative_total=m['negative_clips'], early=m['premature_events'])


def subset(data, indices):
    return {k: data[k][indices] for k in data.files if k != 'device_domain'}


def main():
    base = ROOT / 'artifacts/kws-phase5'
    folder = base / 'pcen-g-int8'
    features = base / 'pcen-g-features'
    output = folder / 'diagnosis.json'
    assert not output.exists(), 'Preserve the completed diagnosis'
    bundle = json.loads((folder / 'model.json').read_text())
    threshold = json.loads((folder / 'threshold.json').read_text())
    checkpoint = base / 'pcen-g/best.pt'
    frozen_paths = [folder / 'model.json', folder / 'threshold.json', checkpoint]
    frozen_paths += [features / name for name in bundle['feature_hashes']]
    before = {str(p.relative_to(ROOT)): digest(p) for p in frozen_paths}
    assert digest(checkpoint) == bundle['checkpoint_sha256']
    assert all(digest(features / name) == sha for name, sha in bundle['feature_hashes'].items())
    torch.set_num_threads(4)

    # Fixed small TRAIN sample: every original, unattenuated recorded example.
    # Select before scoring; neither top failures nor diagnostic TEST recordings.
    train = np.load(features / 'train.npz', allow_pickle=False)
    chosen = np.flatnonzero(train['device_domain'] & (train['variant'] == 0))
    assert 1 <= len(chosen) <= 300
    selected = subset(train, chosen)
    selection = dict(indices=chosen.tolist(), clip_ids=selected['clip_id'].tolist(),
                     source_groups=sorted(set(selected['source_group'].tolist())))
    with (folder / 'diagnosis-train-selection.json').open('x') as f:
        json.dump(selection, f, indent=2)
    q_train, f_train = scores_for(selected, bundle, checkpoint)
    train_metrics = {kind: compact(metrics(scores, selected, threshold[kind]))
                     for kind, scores in [('quantized', q_train), ('floating', f_train)]}

    datasets = {}
    failures = []
    for name in ('validation', 'weak6', 'weak12'):
        data = np.load(features / f'{name}.npz', allow_pickle=False)
        saved = np.load(folder / f'{name}-scores.npz', allow_pickle=False)
        np.testing.assert_array_equal(data['clip_id'], saved['clip_id'])
        indices = np.flatnonzero(data['device_domain'])
        domain = subset(data, indices)
        item = dict(count=len(indices), scores={}, feature_saturation={})
        for kind in ('quantized', 'floating'):
            scores = saved[kind][indices]
            item['scores'][kind] = compact(metrics(scores, domain, threshold[kind]))
            # Descriptive envelope only. Do not publish a replacement threshold.
            curve = []
            for probability in range(500, 951, 10):
                m = metrics(scores, domain, threshold_q8(probability))
                curve.append(dict(probability_per_mille=probability, **compact(m)))
            item.setdefault('threshold_envelope', {})[kind] = curve
            item.setdefault('any_zero_false_full_recall', {})[kind] = any(
                r['negative_hits'] == 0 and r['early'] == 0 and r['hits'] == r['totals'] for r in curve)
            for i in range(len(indices)):
                times = events(scores[i], threshold[kind])
                low, high = int(domain['event_start_accept'][i]), int(domain['event_end'][i])
                positive = bool(domain['label'][i])
                valid = [t for t in times if low <= t <= high + 12800]
                if (positive and not valid) or (not positive and times):
                    failures.append(dict(split=name, kind=kind, clip_id=str(domain['clip_id'][i]),
                                         label=int(positive), text=str(domain['text'][i]),
                                         language=str(domain['language'][i]),
                                         group=str(domain['sampling_group'][i]),
                                         event_samples=times, accept=[low, high + 12800],
                                         late_max_logit_q8=float(scores[i, 128:].max())))
        x = domain['x'][:, 128:]
        for language in ('zh', 'yue'):
            chosen_x = x[(domain['language'] == language) & (domain['label'] == 1)]
            item['feature_saturation'][language] = dict(
                low_fraction=float(np.mean(chosen_x == -128)), high_fraction=float(np.mean(chosen_x == 127)))
        q, f = saved['quantized'][indices, 128:], saved['floating'][indices, 128:]
        item['integer_float_logit_error'] = dict(
            mean_abs=float(np.abs(q - f).mean()), p95_abs=float(np.percentile(np.abs(q - f), 95)),
            median_signed=float(np.median(q - f)), max_abs=float(np.abs(q - f).max()))
        datasets[name] = item

    payload = torch.load(checkpoint, map_location='cpu', weights_only=True)
    assert before == {str(p.relative_to(ROOT)): digest(p) for p in frozen_paths}
    report = dict(complete=True, selected_train=len(chosen), train=train_metrics,
                  checkpoint_step=payload['step'], validation_loss=payload['validation_loss'],
                  datasets=datasets, failures=failures, input_hashes=before,
                  selection_sha256=digest(folder / 'diagnosis-train-selection.json'),
                  source_sha256=digest(Path(__file__)),
                  test_read=False, retrained=False, threshold_changed=False, flashed=False,
                  limitation='Reused development sources. Threshold envelope is diagnostic, not a newly selected operating point.')
    with output.open('x') as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    print(json.dumps(dict(train=train_metrics, checkpoint_step=payload['step'],
                         dataset_summary={k: dict(scores=v['scores'], feasible=v['any_zero_false_full_recall'],
                                                  logit_error=v['integer_float_logit_error']) for k, v in datasets.items()})))


if __name__ == '__main__':
    main()
