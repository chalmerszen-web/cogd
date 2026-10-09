"""One J candidate under Phase6; source freeze, bounded train, no auto flash."""
import hashlib
import json
from pathlib import Path
from train_device_domain import ROOT, OUT, run


def main():
    assert not (OUT/'adapt-j').exists()
    sources=list((ROOT/'training/kws').glob('*.py'))+[
        Path(__file__),ROOT/'tools/kws/evaluate_joint_fusion.py',ROOT/'tools/kws/evaluate_contrast_weak.py']
    freeze={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    with (OUT/'adapt-j-code-freeze.json').open('x') as f:json.dump(freeze,f,indent=2)
    features='artifacts/kws-phase5/features-weak-logmel'
    run('prepare-weak-logmel','training/kws/prepare_weak_logmel.py')
    run('train-adapt-j','training/kws/train.py','--features',features,
        '--out','artifacts/kws-phase5/adapt-j','--initial-checkpoint','artifacts/kws-phase5/adapt-f/best.pt',
        '--steps','4000','--batch','32','--threads','4','--seed','20261004','--learning-rate','.0001',
        '--paired-negatives','--contrast-negatives','--device-fraction','.5')
    run('calibrate-adapt-j','training/kws/calibrate.py','--features',features,
        '--checkpoint','artifacts/kws-phase5/adapt-j/best.pt','--out','artifacts/kws-phase5/adapt-j-int8')
    run('validate-adapt-j','training/kws/evaluate.py','--features',features,
        '--checkpoint','artifacts/kws-phase5/adapt-j/best.pt','--bundle','artifacts/kws-phase5/adapt-j-int8',
        '--split','validation')
    run('validate-fusion-ej','tools/kws/evaluate_joint_fusion.py','--secondary','adapt-j','--smoothing-blocks','3')
    run('weak-fusion-ej','tools/kws/evaluate_contrast_weak.py','--secondary','adapt-j')
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==sha for p,sha in freeze.items())
    report=json.loads((OUT/'fusion-ej-smooth3/weak-validation.json').read_text())
    (OUT/'adapt-j-finished.json').write_text(json.dumps(dict(complete=True,steps=4000,
        gates=report['gates'],advance=report['advance'],test_evaluated=False,flashed=False),indent=2))


if __name__=='__main__':main()
