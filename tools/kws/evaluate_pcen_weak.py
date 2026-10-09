"""Frozen PCEN candidate quality gates, including original and weak validation."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from evaluate import metrics, scores_for


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--candidate', choices=('pcen-g', 'pcen-h'), default='pcen-g')
    args = parser.parse_args()
    base = ROOT / 'artifacts/kws-phase5'
    bundle_path = base / (args.candidate + '-int8')
    features = base / 'pcen-g-features'
    bundle = json.loads((bundle_path / 'model.json').read_text())
    frozen = json.loads((bundle_path / 'threshold.json').read_text())
    original = json.loads((bundle_path / 'validation-metrics.json').read_text())['quantized']
    assert bundle['frontend'] == 'pcen_v1' and bundle['name'].endswith('_pcen_v1')
    for name, expected in bundle['feature_hashes'].items():
        assert hashlib.sha256((features / name).read_bytes()).hexdigest() == expected
    checkpoint = base / args.candidate / 'best.pt'
    assert hashlib.sha256(checkpoint.read_bytes()).hexdigest() == bundle['checkpoint_sha256']
    assert not (bundle_path / 'weak-validation.json').exists()
    torch.set_num_threads(4)
    results = {}
    baseline = json.loads((base / 'noise-frontend-pilot/report.json').read_text())['results']
    for db in (6, 12):
        data = np.load(features / f'weak{db}.npz')
        assert len(data['x']) == 88 and data['device_domain'].all()
        quantized, floating = scores_for(data, bundle, checkpoint)
        results[str(db)] = dict(quantized=metrics(quantized, data, frozen['quantized']),
                                floating=metrics(floating, data, frozen['floating']))
        np.savez_compressed(bundle_path / f'weak{db}-scores.npz', quantized=quantized, floating=floating, clip_id=data['clip_id'])
    captured = original['device']
    gate = dict(captured_recall=all(x['hits'] == 8 for x in captured['languages'].values()),
                captured_negatives=captured['negative_triggered_clips'] == 0,
                overall_recall=all(x['recall'] >= .9 for x in original['languages'].values()),
                overall_negatives=original['negative_triggered_clips'] / original['negative_clips'] <= .05,
                no_partial=original['partial_word_triggers'] == 0,
                weak_nonregression=all(results[str(db)]['quantized']['languages'][lang]['hits'] >= baseline[str(db)]['baseline']['languages'][lang]['hits'] for db in (6, 12) for lang in ('zh', 'yue')),
                weak_negatives=all(results[str(db)]['quantized']['negative_triggered_clips'] <= baseline[str(db)]['baseline']['negative_triggered_clips'] for db in (6, 12)))
    extra = sum(results[str(db)]['quantized']['languages']['yue']['hits'] - baseline[str(db)]['baseline']['languages']['yue']['hits'] for db in (6, 12))
    gate['yue_improves'] = extra >= 2
    report = dict(complete=True, candidate=args.candidate, threshold=frozen, original=original, weak=results,
                  gates=gate, weak_yue_extra_hits=extra, advance=all(gate.values()),
                  model_sha256=hashlib.sha256((bundle_path / 'model.json').read_bytes()).hexdigest(),
                  limitation='Development validation with reused voices and approximate weak mixtures; no physical5m or newly blind test.')
    (bundle_path / 'weak-validation.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(dict(threshold=frozen, captured=captured['languages'], weak={db: r['quantized']['languages'] for db, r in results.items()}, gates=gate, advance=report['advance'])))


if __name__ == '__main__':
    main()
