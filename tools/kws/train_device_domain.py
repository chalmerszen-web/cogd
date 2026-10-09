"""One bounded adaptation candidate; stops after validation, never flashes."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/kws-phase5'
PYTHON='/home/chalmers/.venvs/cogd-kws/bin/python'
POSIX='/mnt/'+ROOT.drive[0].lower()+ROOT.as_posix()[2:]


def run(name,*arguments):
    command=['wsl','-d','Ubuntu','--cd',POSIX,PYTHON,*arguments]
    with (OUT/(name+'.log')).open('xb') as stream:
        result=subprocess.run(command,stdout=stream,stderr=subprocess.STDOUT)
    if result.returncode:raise RuntimeError(name+' failed; preserve evidence')
    print(name+' completed',flush=True)


def main():
    corpus=json.loads((OUT/'corpus/report.json').read_text(encoding='utf8'))
    assert corpus['complete'] and len(corpus['trials'])==64
    supplement=json.loads((OUT/'corpus-zh-negatives/report.json').read_text(encoding='utf8'))
    assert supplement['complete'] and len(supplement['trials'])==12
    assert not (OUT/'adapt-a').exists()
    sources=list((ROOT/'training/kws').glob('*.py'))+[Path(__file__)]
    freeze={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    (OUT/'adapt-a-code-freeze.json').write_text(json.dumps(freeze,indent=2))
    run('prepare-device','training/kws/device_domain.py','--corpus','artifacts/kws-phase5/corpus',
        '--supplement','artifacts/kws-phase5/corpus-zh-negatives',
        '--out','artifacts/kws-phase5/features')
    run('train-adapt-a','training/kws/train.py','--features','artifacts/kws-phase5/features',
        '--out','artifacts/kws-phase5/adapt-a','--initial-checkpoint','artifacts/kws-phase3/trained/best.pt',
        '--steps','3000','--batch','32','--threads','4','--seed','20260923','--learning-rate','.0003',
        '--balanced-hard-negatives','--device-fraction','.5')
    run('calibrate-adapt-a','training/kws/calibrate.py','--features','artifacts/kws-phase5/features',
        '--checkpoint','artifacts/kws-phase5/adapt-a/best.pt','--out','artifacts/kws-phase5/adapt-a-int8')
    run('validate-adapt-a','training/kws/evaluate.py','--features','artifacts/kws-phase5/features',
        '--checkpoint','artifacts/kws-phase5/adapt-a/best.pt','--bundle','artifacts/kws-phase5/adapt-a-int8',
        '--split','validation')
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in freeze.items())
    (OUT/'adapt-a-finished.json').write_text(json.dumps(dict(complete=True,steps=3000,test_evaluated=False,flashed=False),indent=2))


if __name__=='__main__':main()
