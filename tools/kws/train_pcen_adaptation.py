"""One measured-domain H adaptation after G/capacity diagnosis; never flashes."""
import hashlib
import json
from pathlib import Path

from train_device_domain import ROOT, OUT, run


def main():
    capacity = json.loads((OUT / 'capacity-probe/report.json').read_text())
    assert capacity['complete'] and capacity['results'][0]['fits_small_train_set']
    assert not json.loads((OUT / 'pcen-g-int8/weak-validation.json').read_text())['advance']
    assert not (OUT / 'pcen-h').exists()
    inputs = list((ROOT / 'training/kws').glob('*.py'))
    inputs += [Path(__file__), ROOT / 'tools/kws/evaluate_pcen_weak.py', OUT / 'pcen-g/best.pt']
    inputs += list((OUT / 'pcen-g-features').glob('*.npz'))
    inputs += list((OUT / 'pcen-g-features').glob('*.json'))
    freeze = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    with (OUT / 'pcen-h-code-input-freeze.json').open('x') as f:
        json.dump(freeze, f, indent=2)
    features = 'artifacts/kws-phase5/pcen-g-features'
    checkpoint = 'artifacts/kws-phase5/pcen-h/best.pt'
    bundle = 'artifacts/kws-phase5/pcen-h-int8'
    run('train-pcen-h', 'training/kws/train.py', '--features', features,
        '--out', 'artifacts/kws-phase5/pcen-h', '--steps', '2000', '--batch', '32', '--threads', '4',
        '--seed', '20261002', '--learning-rate', '.0001', '--paired-negatives', '--device-fraction', '.75',
        '--initial-checkpoint', 'artifacts/kws-phase5/pcen-g/best.pt')
    run('calibrate-pcen-h', 'training/kws/calibrate.py', '--features', features,
        '--checkpoint', checkpoint, '--out', bundle)
    run('validate-pcen-h', 'training/kws/evaluate.py', '--features', features,
        '--checkpoint', checkpoint, '--bundle', bundle, '--split', 'validation')
    run('weak-pcen-h', 'tools/kws/evaluate_pcen_weak.py', '--candidate', 'pcen-h')
    assert all(hashlib.sha256((ROOT / p).read_bytes()).hexdigest() == sha for p, sha in freeze.items())
    passed = json.loads((OUT / 'pcen-h-int8/weak-validation.json').read_text())['advance']
    (OUT / 'pcen-h-finished.json').write_text(json.dumps(dict(complete=True, steps=2000,
        test_evaluated=False, flashed=False, host_gates=passed), indent=2))


if __name__ == '__main__':
    main()
