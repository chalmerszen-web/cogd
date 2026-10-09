"""One finite full-phrase candidate after B's physical partial-word failures."""
import hashlib
import json
from pathlib import Path
from train_device_domain import ROOT,OUT,run


def main():
    assert not (OUT/'adapt-c').exists()
    sources=list((ROOT/'training/kws').glob('*.py'))+[Path(__file__)]
    freeze={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    (OUT/'adapt-c-code-freeze.json').write_text(json.dumps(freeze,indent=2))
    run('prepare-paired-c','training/kws/paired_negative.py','--out','artifacts/kws-phase5/features-paired')
    run('train-adapt-c','training/kws/train.py','--features','artifacts/kws-phase5/features-paired',
        '--out','artifacts/kws-phase5/adapt-c','--initial-checkpoint','artifacts/kws-phase5/adapt-b/best.pt',
        '--steps','4000','--batch','32','--threads','4','--seed','20260925','--learning-rate','.0001',
        '--paired-negatives','--device-fraction','.25')
    run('calibrate-adapt-c','training/kws/calibrate.py','--features','artifacts/kws-phase5/features-paired',
        '--checkpoint','artifacts/kws-phase5/adapt-c/best.pt','--out','artifacts/kws-phase5/adapt-c-int8')
    run('validate-adapt-c','training/kws/evaluate.py','--features','artifacts/kws-phase5/features-paired',
        '--checkpoint','artifacts/kws-phase5/adapt-c/best.pt','--bundle','artifacts/kws-phase5/adapt-c-int8',
        '--split','validation')
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in freeze.items())
    (OUT/'adapt-c-finished.json').write_text(json.dumps(dict(complete=True,steps=4000,test_evaluated=False,flashed=False),indent=2))


if __name__=='__main__':main()
