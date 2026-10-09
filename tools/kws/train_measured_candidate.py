"""One data-coverage experiment with fixed C training settings, no deployment."""
import hashlib
import json
from pathlib import Path
from train_device_domain import ROOT,OUT,run


def main():
    audit=json.loads((OUT/'full-phrase-capture-audit.json').read_text())
    assert audit['complete'] and sum(r['eligible'] for r in audit['trials'])>=24
    assert not (OUT/'adapt-e').exists()
    sources=list((ROOT/'training/kws').glob('*.py'))+[Path(__file__)]
    freeze={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    (OUT/'adapt-e-code-freeze-v2.json').write_text(json.dumps(freeze,indent=2))
    run('prepare-measured-e-v2','training/kws/append_captures.py',
        '--corpus','artifacts/kws-phase5/corpus-full-phrase',
        '--audit','artifacts/kws-phase5/full-phrase-capture-audit.json',
        '--out','artifacts/kws-phase5/features-measured-v2')
    run('train-adapt-e','training/kws/train.py','--features','artifacts/kws-phase5/features-measured-v2',
        '--out','artifacts/kws-phase5/adapt-e','--initial-checkpoint','artifacts/kws-phase5/adapt-b/best.pt',
        '--steps','4000','--batch','32','--threads','4','--seed','20260925','--learning-rate','.0001',
        '--paired-negatives','--device-fraction','.25')
    run('calibrate-adapt-e','training/kws/calibrate.py','--features','artifacts/kws-phase5/features-measured-v2',
        '--checkpoint','artifacts/kws-phase5/adapt-e/best.pt','--out','artifacts/kws-phase5/adapt-e-int8')
    run('validate-adapt-e','training/kws/evaluate.py','--features','artifacts/kws-phase5/features-measured-v2',
        '--checkpoint','artifacts/kws-phase5/adapt-e/best.pt','--bundle','artifacts/kws-phase5/adapt-e-int8',
        '--split','validation')
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in freeze.items())
    (OUT/'adapt-e-finished.json').write_text(json.dumps(dict(complete=True,steps=4000,test_evaluated=False,flashed=False),indent=2))


if __name__=='__main__':main()
