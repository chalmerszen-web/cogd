"""Bind candidate B to host-C proof and stage its source; not final acceptance."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from train_device_domain import ROOT,OUT,POSIX,PYTHON


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def run(name,exe,*arguments):
    with (OUT/(name+'.log')).open('xb') as f:
        r=subprocess.run(['wsl','-d','Ubuntu','--cd',POSIX,exe,*arguments],stdout=f,stderr=subprocess.STDOUT)
    if r.returncode:raise RuntimeError(name+' failed')


def main():
    bundle=OUT/'adapt-b-int8'
    assert json.loads((OUT/'adapt-b-finished.json').read_text())['complete']
    assert not (OUT/'near-candidate.json').exists()
    metrics=json.loads((bundle/'validation-metrics.json').read_text())
    assert metrics['quantized']['device']['negative_triggered_clips']==0
    checks=json.loads((OUT/'adapt-b-device-diagnosis.json').read_text())
    assert all(r['hit'] for r in checks if r['gain']==.35)
    threshold=json.loads((bundle/'threshold.json').read_text())
    files=[bundle/'model.c',bundle/'model.json',bundle/'threshold.json',OUT/'adapt-b/best.pt']
    selection=dict(candidate='adapt-b',purpose='near-field experiment, weak-signal and five-metre not passed',
        threshold=threshold,files={p.relative_to(ROOT).as_posix():sha(p) for p in files},
        test_used_for_selection=False)
    (OUT/'near-candidate.json').write_text(json.dumps(selection,indent=2))
    cmake='/home/chalmers/.local/lib/python3.12/site-packages/cmake/data/bin/cmake'
    run('candidate-host-configure',cmake,'-S','tests/kws','-B','build-kws-phase5-host','-G','Ninja',
        '-DCMAKE_MAKE_PROGRAM=/home/chalmers/.local/bin/ninja',
        '-DKWS_MODEL_SOURCE='+POSIX+'/artifacts/kws-phase5/adapt-b-int8/model.c')
    run('candidate-host-build',cmake,'--build','build-kws-phase5-host','--target','kws')
    run('candidate-host-parity',PYTHON,'tools/kws/parity.py','--model','artifacts/kws-phase5/adapt-b-int8/model.json',
        '--library','build-kws-phase5-host/libkws.so','--symbol','kws_trained_model','--out','artifacts/kws-phase5/host-parity')
    parity=json.loads((OUT/'host-parity/host-parity.json').read_text())
    assert parity['passed'] and parity['trained'] and parity['model_json_sha256']==sha(bundle/'model.json')
    source=ROOT/'components/kws_c11/generated/trained.c'
    assert sha(source)==sha(ROOT/'artifacts/kws-phase3/int8/model.c')
    shutil.copyfile(source,OUT/'previous-generated-trained.c');shutil.copyfile(bundle/'model.c',source)
    assert all(sha(ROOT/p)==h for p,h in selection['files'].items())
    print(json.dumps(dict(staged=True,threshold=threshold['device_threshold_per_mille'],workspace=parity['workspace'])))


if __name__=='__main__':main()
