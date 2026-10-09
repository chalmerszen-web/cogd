"""Prepare a separate, explicitly provisional E/K firmware comparison."""
import json
from pathlib import Path
from stage_device_candidate import run,sha
from train_device_domain import ROOT,OUT,POSIX,PYTHON


def main():
    directory=OUT/'fusion-ek-smooth3'
    report=json.loads((directory/'validation-metrics.json').read_text())
    assert all(report['host_gate'].values())
    weak=json.loads((directory/'weak-validation.json').read_text())
    plan=json.loads((directory/'plan.json').read_text())
    for candidate,files in plan['sources'].items():
        for name,expected in files.items():assert sha(OUT/(candidate+'-int8')/name)==expected
    primary=ROOT/'components/kws_c11/generated/fusion_primary.c'
    assert primary.read_text()==(OUT/'adapt-e-int8/model.c').read_text()
    source=(OUT/'adapt-k-int8/model.c').read_text()
    assert source.count('kws_trained_model')==1
    secondary=ROOT/'components/kws_c11/generated/fusion_secondary_k.c'
    with secondary.open('x',encoding='utf8',newline='\n') as f:
        f.write(source.replace('kws_trained_model','kws_secondary_model'))
    cmake='/home/chalmers/.local/lib/python3.12/site-packages/cmake/data/bin/cmake'
    build='build-kws-fusion-ek-host'
    run('fusion-ek-host-configure',cmake,'-S','tests/kws','-B',build,'-G','Ninja',
        '-DCMAKE_MAKE_PROGRAM=/home/chalmers/.local/bin/ninja',
        '-DKWS_MODEL_SOURCE='+POSIX+'/components/kws_c11/generated/fusion_primary.c',
        '-DKWS_SECONDARY_SOURCE='+POSIX+'/components/kws_c11/generated/fusion_secondary_k.c')
    run('fusion-ek-host-build',cmake,'--build',build)
    run('fusion-ek-host-sanitizers',cmake,'--build',build,'--target','test')
    for candidate,symbol in (('adapt-e','kws_trained_model'),('adapt-k','kws_secondary_model')):
        run('fusion-ek-parity-'+candidate,PYTHON,'tools/kws/parity.py',
            '--model','artifacts/kws-phase5/'+candidate+'-int8/model.json',
            '--library',build+'/libkws.so','--symbol',symbol,
            '--out','artifacts/kws-phase5/fusion-ek-smooth3/'+candidate+'-parity')
    run('fusion-ek-shared-parity',PYTHON,'tools/kws/fusion_parity.py','--pair','ek')
    # USB primary traces must match the exact shared-frontend vector.
    for name in ('fixed-traces.json','host-parity.json'):
        (directory/name).write_bytes((directory/'adapt-e-parity'/name).read_bytes())
    (directory/'member-c-parity.json').write_text(json.dumps(dict(passed=True,
        library_sha256=sha(ROOT/build/'libkws.so'),experimental_comparison_only=True,
        weak_gates_passed=weak['advance'],
        model_sources={p.name:sha(p) for p in (primary,secondary)}),indent=2))
    print('E/K C parity passed; provisional physical comparison only',flush=True)


if __name__=='__main__':main()
