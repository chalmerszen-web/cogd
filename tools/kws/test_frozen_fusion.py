"""Once-only frozen E/F test scoring; never select weights or thresholds here."""
import hashlib
import json
from pathlib import Path
import sys

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from evaluate import metrics, scores_for


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def combine(a, b, integer):
    values = (a.astype(np.float64) + b.astype(np.float64)) / 2
    if integer:
        values = np.trunc(values).astype(np.int16)
    blocks = values[:, 1::2].astype(np.float64)
    for i in range(blocks.shape[1]):
        average = blocks[:, max(0, i - 2):i + 1].mean(1)
        values[:, 2 * i + 1] = np.trunc(average) if integer else average
    return values


def main():
    base = ROOT / 'artifacts/kws-phase5'
    frozen = base / 'fusion-ef-smooth3'
    out = frozen / 'frozen-test'
    out.mkdir(exist_ok=False)
    torch.set_num_threads(4)
    plan = json.loads((frozen / 'plan.json').read_text())
    threshold = json.loads((frozen / 'threshold.json').read_text())
    assert plan['coefficients'] == [1, 1] and plan['smoothing_blocks'] == 3
    assert threshold['quantized'] == 281 and threshold['device_threshold_per_mille'] == 750
    features = base / 'features-measured-v2'
    test = np.load(features / 'test.npz', allow_pickle=False)
    reference = np.load(frozen / 'validation-scores.npz', allow_pickle=False)
    members = []
    validation = []
    sources = {}
    for candidate in ('adapt-e', 'adapt-f'):
        bundle_dir = base / (candidate + '-int8')
        checkpoint = base / candidate / 'best.pt'
        for name, expected in plan['sources'][candidate].items():
            assert digest(bundle_dir / name) == expected, (candidate, name)
        bundle = json.loads((bundle_dir / 'model.json').read_text())
        assert digest(checkpoint) == bundle['checkpoint_sha256']
        for name, expected in bundle['feature_hashes'].items():
            assert digest(features / name) == expected, name
        saved = np.load(bundle_dir / 'validation-scores.npz', allow_pickle=False)
        assert np.array_equal(saved['clip_id'], reference['clip_id'])
        validation.append(saved)
        sources[candidate] = dict(model_sha256=digest(bundle_dir / 'model.json'),
                                  checkpoint_sha256=digest(checkpoint))
        members.append(scores_for(test, bundle, checkpoint))
    report = dict(threshold=threshold, sources=sources,
                  features_sha256=digest(features / 'test.npz'),
                  script_sha256=digest(Path(__file__)),
                  interpretation='Frozen regression on Phase3 reserved synthetic sources; some were previously replayed for B. Not a newly blind test or human/5m proof.')
    outputs = {}
    for index, kind in enumerate(('quantized', 'floating')):
        reproduced = combine(validation[0][kind], validation[1][kind], index == 0)
        assert np.array_equal(reproduced, reference[kind]), kind
        outputs[kind] = combine(members[0][index], members[1][index], index == 0)
        report[kind] = metrics(outputs[kind], test, threshold[kind])
    np.savez_compressed(out / 'scores.npz', **outputs, clip_id=test['clip_id'])
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
