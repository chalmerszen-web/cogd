"""Stage only the validation-passing fixed E/F fusion and prove each C member."""
import hashlib
import json
from pathlib import Path
from stage_device_candidate import run,sha
from train_device_domain import ROOT,OUT,POSIX,PYTHON


def main():
    directory=OUT/'fusion-ef-smooth3';report=json.loads((directory/'validation-metrics.json').read_text())
    assert report['ready_for_c_prototype'] and all(report['host_gate'].values())
    plan=json.loads((directory/'plan.json').read_text())
    for candidate,files in plan['sources'].items():
        for name,expected in files.items():assert sha(OUT/(candidate+'-int8')/name)==expected
    for candidate,name,symbol in (('adapt-e','fusion_primary.c','kws_trained_model'),('adapt-f','fusion_secondary.c','kws_secondary_model')):
        source=(OUT/(candidate+'-int8')/'model.c').read_text()
        assert source.count('kws_trained_model')==1
        destination=ROOT/'components/kws_c11/generated'/name
        with destination.open('x',encoding='utf8',newline='\n') as f:f.write(source.replace('kws_trained_model',symbol))
    cmake='/home/chalmers/.local/lib/python3.12/site-packages/cmake/data/bin/cmake'
    run('fusion-host-configure',cmake,'-S','tests/kws','-B','build-kws-fusion-host','-G','Ninja',
        '-DCMAKE_MAKE_PROGRAM=/home/chalmers/.local/bin/ninja',
        '-DKWS_MODEL_SOURCE='+POSIX+'/components/kws_c11/generated/fusion_primary.c',
        '-DKWS_SECONDARY_SOURCE='+POSIX+'/components/kws_c11/generated/fusion_secondary.c')
    run('fusion-host-build',cmake,'--build','build-kws-fusion-host')
    run('fusion-host-sanitizers',cmake,'--build','build-kws-fusion-host','--target','test')
    for candidate,symbol in (('adapt-e','kws_trained_model'),('adapt-f','kws_secondary_model')):
        run('fusion-parity-'+candidate,PYTHON,'tools/kws/parity.py','--model','artifacts/kws-phase5/'+candidate+'-int8/model.json',
            '--library','build-kws-fusion-host/libkws.so','--symbol',symbol,
            '--out','artifacts/kws-phase5/fusion-ef-smooth3/'+candidate+'-parity')
    (directory/'member-c-parity.json').write_text(json.dumps(dict(passed=True,library_sha256=sha(ROOT/'build-kws-fusion-host/libkws.so'),
        model_sources={name:sha(ROOT/'components/kws_c11/generated'/name) for name in ('fusion_primary.c','fusion_secondary.c')}),indent=2))


if __name__=='__main__':main()
