"""One bounded K recovery from J-last; no automatic installation."""
import hashlib
import json
from pathlib import Path
from train_device_domain import ROOT, OUT, run


def main():
    assert json.loads((OUT/'adapt-j-finished.json').read_text())['complete']
    assert not (OUT/'adapt-k').exists()
    sources=list((ROOT/'training/kws').glob('*.py'))+[
        Path(__file__),ROOT/'tools/kws/evaluate_joint_fusion.py',ROOT/'tools/kws/evaluate_contrast_weak.py']
    freeze={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    with (OUT/'adapt-k-code-freeze.json').open('x') as f:json.dump(freeze,f,indent=2)
    features='artifacts/kws-phase5/features-weak-logmel'
    run('train-adapt-k','training/kws/train.py','--features',features,
        '--out','artifacts/kws-phase5/adapt-k','--initial-checkpoint','artifacts/kws-phase5/adapt-j/last.pt',
        '--steps','2000','--batch','32','--threads','4','--seed','20261005','--learning-rate','.00005',
        '--paired-negatives','--contrast-negatives','--device-fraction','.5','--negative-peak-weight','.15')
    run('calibrate-adapt-k','training/kws/calibrate.py','--features',features,
        '--checkpoint','artifacts/kws-phase5/adapt-k/best.pt','--out','artifacts/kws-phase5/adapt-k-int8')
    run('validate-adapt-k','training/kws/evaluate.py','--features',features,
        '--checkpoint','artifacts/kws-phase5/adapt-k/best.pt','--bundle','artifacts/kws-phase5/adapt-k-int8',
        '--split','validation')
    run('validate-fusion-ek','tools/kws/evaluate_joint_fusion.py','--secondary','adapt-k','--smoothing-blocks','3')
    run('weak-fusion-ek','tools/kws/evaluate_contrast_weak.py','--secondary','adapt-k')
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==sha for p,sha in freeze.items())
    report=json.loads((OUT/'fusion-ek-smooth3/weak-validation.json').read_text())
    (OUT/'adapt-k-finished.json').write_text(json.dumps(dict(complete=True,steps=2000,
        gates=report['gates'],advance=report['advance'],test_evaluated=False,flashed=False),indent=2))


if __name__=='__main__':main()
