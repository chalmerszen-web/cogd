"""Second predeclared device-adaptation candidate; validation only, no flashing."""
import hashlib
import json
from pathlib import Path
from train_device_domain import ROOT,OUT,run


def main():
    features=OUT/'features-channel'
    report=json.loads((features/'features.json').read_text())
    assert report['complete'] and report['synthetic_examples']==3200
    assert hashlib.sha256((features/'validation.npz').read_bytes()).hexdigest()==report['validation_unchanged']
    assert not (OUT/'adapt-b').exists()
    sources=list((ROOT/'training/kws').glob('*.py'))+[Path(__file__)]
    freeze={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    (OUT/'adapt-b-code-freeze.json').write_text(json.dumps(freeze,indent=2))
    run('train-adapt-b','training/kws/train.py','--features','artifacts/kws-phase5/features-channel',
        '--out','artifacts/kws-phase5/adapt-b','--initial-checkpoint','artifacts/kws-phase3/trained/best.pt',
        '--steps','6000','--batch','32','--threads','4','--seed','20260924','--learning-rate','.0001',
        '--balanced-hard-negatives','--device-fraction','.25')
    run('calibrate-adapt-b','training/kws/calibrate.py','--features','artifacts/kws-phase5/features-channel',
        '--checkpoint','artifacts/kws-phase5/adapt-b/best.pt','--out','artifacts/kws-phase5/adapt-b-int8')
    run('validate-adapt-b','training/kws/evaluate.py','--features','artifacts/kws-phase5/features-channel',
        '--checkpoint','artifacts/kws-phase5/adapt-b/best.pt','--bundle','artifacts/kws-phase5/adapt-b-int8',
        '--split','validation')
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in freeze.items())
    (OUT/'adapt-b-finished.json').write_text(json.dumps(dict(complete=True,steps=6000,test_evaluated=False,flashed=False),indent=2))


if __name__=='__main__':main()
