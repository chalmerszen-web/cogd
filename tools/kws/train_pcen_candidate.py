"""One source-audited6000step PCEN run; no test evaluation or automatic flash."""
import hashlib
import json
from pathlib import Path
from train_device_domain import ROOT, OUT, run


def main():
    dataset = OUT / 'pcen-g-features'
    metadata = json.loads((dataset / 'features.json').read_text(encoding='utf8'))
    assert metadata['complete'] and metadata['frontend'] == 'pcen_v1'
    assert not metadata['test_waveforms_read'] and not metadata['diagnostic_recordings_used']
    assert not (OUT / 'pcen-g').exists()
    sources = list((ROOT / 'training/kws').glob('*.py')) + [Path(__file__)]
    freeze = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    with (OUT / 'pcen-g-code-freeze.json').open('x') as f:
        json.dump(freeze, f, indent=2)
    run('train-pcen-g', 'training/kws/train.py', '--features', 'artifacts/kws-phase5/pcen-g-features',
        '--out', 'artifacts/kws-phase5/pcen-g', '--steps', '6000', '--batch', '32', '--threads', '4',
        '--seed', '20260930', '--learning-rate', '.001', '--paired-negatives', '--device-fraction', '.25')
    run('calibrate-pcen-g', 'training/kws/calibrate.py', '--features', 'artifacts/kws-phase5/pcen-g-features',
        '--checkpoint', 'artifacts/kws-phase5/pcen-g/best.pt', '--out', 'artifacts/kws-phase5/pcen-g-int8')
    run('validate-pcen-g', 'training/kws/evaluate.py', '--features', 'artifacts/kws-phase5/pcen-g-features',
        '--checkpoint', 'artifacts/kws-phase5/pcen-g/best.pt', '--bundle', 'artifacts/kws-phase5/pcen-g-int8', '--split', 'validation')
    assert all(hashlib.sha256((ROOT / p).read_bytes()).hexdigest() == h for p, h in freeze.items())
    (OUT / 'pcen-g-finished.json').write_text(json.dumps(dict(complete=True, steps=6000, test_evaluated=False, flashed=False), indent=2))


if __name__ == '__main__':
    main()
