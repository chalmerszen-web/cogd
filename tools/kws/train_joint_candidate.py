"""One fresh joint-domain initialization, same architecture and frozen dataset."""
import hashlib
import json
from pathlib import Path
from train_device_domain import ROOT,OUT,run


def main():
    assert not (OUT/'adapt-f').exists()
    sources=list((ROOT/'training/kws').glob('*.py'))+[Path(__file__)]
    freeze={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    (OUT/'adapt-f-code-freeze.json').write_text(json.dumps(freeze,indent=2))
    run('train-adapt-f','training/kws/train.py','--features','artifacts/kws-phase5/features-measured-v2',
        '--out','artifacts/kws-phase5/adapt-f','--steps','6000','--batch','32','--threads','4',
        '--seed','20260928','--learning-rate','.001','--paired-negatives','--device-fraction','.25')
    run('calibrate-adapt-f','training/kws/calibrate.py','--features','artifacts/kws-phase5/features-measured-v2',
        '--checkpoint','artifacts/kws-phase5/adapt-f/best.pt','--out','artifacts/kws-phase5/adapt-f-int8')
    run('validate-adapt-f','training/kws/evaluate.py','--features','artifacts/kws-phase5/features-measured-v2',
        '--checkpoint','artifacts/kws-phase5/adapt-f/best.pt','--bundle','artifacts/kws-phase5/adapt-f-int8',
        '--split','validation')
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in freeze.items())
    (OUT/'adapt-f-finished.json').write_text(json.dumps(dict(complete=True,steps=6000,test_evaluated=False,flashed=False),indent=2))


if __name__=='__main__':main()
