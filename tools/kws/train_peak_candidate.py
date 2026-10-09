"""One bounded loss-function experiment; no new data, test scoring or flashing."""
import hashlib
import json
from pathlib import Path
from train_device_domain import ROOT,OUT,run


def main():
    assert not (OUT/'adapt-d').exists()
    assert not json.loads((OUT/'adapt-c-comparison.json').read_text())['ready_for_device_experiment']
    sources=list((ROOT/'training/kws').glob('*.py'))+[Path(__file__)]
    freeze={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    (OUT/'adapt-d-code-freeze.json').write_text(json.dumps(freeze,indent=2))
    run('train-adapt-d','training/kws/train.py','--features','artifacts/kws-phase5/features-paired',
        '--out','artifacts/kws-phase5/adapt-d','--initial-checkpoint','artifacts/kws-phase5/adapt-c/best.pt',
        '--steps','3000','--batch','32','--threads','4','--seed','20260926','--learning-rate','.00005',
        '--paired-negatives','--device-fraction','.25','--negative-peak-weight','.15')
    run('calibrate-adapt-d','training/kws/calibrate.py','--features','artifacts/kws-phase5/features-paired',
        '--checkpoint','artifacts/kws-phase5/adapt-d/best.pt','--out','artifacts/kws-phase5/adapt-d-int8')
    run('validate-adapt-d','training/kws/evaluate.py','--features','artifacts/kws-phase5/features-paired',
        '--checkpoint','artifacts/kws-phase5/adapt-d/best.pt','--bundle','artifacts/kws-phase5/adapt-d-int8',
        '--split','validation')
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in freeze.items())
    (OUT/'adapt-d-finished.json').write_text(json.dumps(dict(complete=True,steps=3000,test_evaluated=False,flashed=False),indent=2))


if __name__=='__main__':main()
