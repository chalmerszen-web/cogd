"""Freeze one completed run, evaluate once and verify its actual C export.

No data generation, training, threshold tuning or firmware installation occurs
here. A failed quality gate is a recorded outcome, not permission for another run.
"""
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'artifacts/kws-phase3'
PYTHON='/home/chalmers/.venvs/cogd-kws/bin/python'
CMAKE='/home/chalmers/.local/lib/python3.12/site-packages/cmake/data/bin/cmake'
NINJA='/home/chalmers/.local/bin/ninja'
POSIX_ROOT='/mnt/'+ROOT.drive[0].lower()+ROOT.as_posix()[2:]


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf8'))


def run(log,executable,*arguments,allowed=(0,)):
    command=['wsl','-d','Ubuntu','--cd',POSIX_ROOT,executable,*arguments]
    with (BASE/log).open('xb') as stream:
        result=subprocess.run(command,stdout=stream,stderr=subprocess.STDOUT)
    if result.returncode not in allowed:raise RuntimeError(f'{log}: exit {result.returncode}; preserve failure')
    return result.returncode


def main():
    assert (BASE/'training-finished.txt').is_file(),'The training worker has not completed'
    assert not (BASE/'selected-model.json').exists(),'Already frozen; do not repeat evaluation'
    frozen=read(BASE/'pretraining-freeze.json')
    for name,expected in frozen['sha256'].items():assert sha(ROOT/name)==expected,('Code drift',name)
    config=read(BASE/'trained/config.json');history=read(BASE/'trained/history.json')
    assert (config['steps'],config['batch'],config['seed'])==(6000,32,20260922)
    assert config['balanced_hard_negatives'] and config['boundary_negative_weight']==1
    assert history[-1]['step']==6000
    assert read(BASE/'dataset/audit.json')['complete']
    bundle=BASE/'int8';validation=read(bundle/'validation-metrics.json')
    files=[bundle/'model.c',bundle/'model.json',bundle/'threshold.json',bundle/'validation-metrics.json',
        BASE/'trained/best.pt',BASE/'trained/config.json',BASE/'features/test.npz',BASE/'dataset/audit.json']
    assert not (bundle/'test-metrics.json').exists()
    selection=dict(selected_at=datetime.now(timezone.utc).isoformat(),budget_runs=1,training_complete=True,
        reason='Only predeclared candidate; best validation-loss checkpoint and validation-only thresholds.',
        validation=validation,threshold=read(bundle/'threshold.json'),
        files={p.relative_to(ROOT).as_posix():sha(p) for p in files},heldout_test_evaluated_at_selection=False)
    with (BASE/'selected-model.json').open('x',encoding='utf8') as stream:json.dump(selection,stream,indent=2)
    run('test.log',PYTHON,'-m','training.kws.cli','evaluate','--features','artifacts/kws-phase3/features',
        '--checkpoint','artifacts/kws-phase3/trained/best.pt','--bundle','artifacts/kws-phase3/int8','--split','test')
    gate_exit=run('quality-gate.log',PYTHON,'training/kws/quality_gate.py','--bundle','artifacts/kws-phase3/int8',
        '--features','artifacts/kws-phase3/features','--dataset','artifacts/kws-phase3/dataset','--partial-word-gate',allowed=(0,2))
    run('host-configure.log',CMAKE,'-S','tests/kws','-B','build-kws-phase3-host','-G','Ninja',
        '-DCMAKE_MAKE_PROGRAM='+NINJA,'-DKWS_MODEL_SOURCE='+POSIX_ROOT+'/artifacts/kws-phase3/int8/model.c')
    run('host-build.log',CMAKE,'--build','build-kws-phase3-host','--target','kws')
    run('host-parity.log',PYTHON,'tools/kws/parity.py','--model','artifacts/kws-phase3/int8/model.json',
        '--library','build-kws-phase3-host/libkws.so','--symbol','kws_trained_model','--out','artifacts/kws-phase3/host-parity')
    parity=read(BASE/'host-parity/host-parity.json')
    assert parity['passed'] and parity['trained'] and parity['workspace']<=20*1024
    assert parity['model_json_sha256']==sha(bundle/'model.json')
    for name,expected in selection['files'].items():assert sha(ROOT/name)==expected,('Frozen artifact changed',name)
    previous=read(ROOT/'artifacts/kws-phase2/final-stage-audit.json')
    for item in previous['files']:assert sha(ROOT/item['path'])==item['sha256']
    # Explicitly retain a derived centre without mistaking it for phonetic truth.
    centres=[]
    for line in (BASE/'dataset/manifest.jsonl').read_text(encoding='utf8').splitlines():
        row=json.loads(line)
        if row.get('endpoint_schema')==1:
            centres.append(dict(clip_id=row['clip_id'],split=row['split'],
                end_centre_sample=(row['speech_end_low_sample']+row['speech_end_high_sample'])/2,
                uncertainty_samples=row['speech_end_high_sample']-row['speech_end_low_sample']))
    with (BASE/'endpoint-centres.jsonl').open('x',encoding='utf8') as stream:
        for row in centres:stream.write(json.dumps(row)+'\n')
    report=dict(single_run_completed=True,training_steps=6000,quality_passed=gate_exit==0,
        quality=read(bundle/'quality-gate.json'),test=read(bundle/'test-metrics.json'),host_parity=parity,
        old_stage_artifacts_unchanged=True,selected_bundle_unchanged=True,firmware_installed=False,
        limitation='Host training/evaluation only; no real speaker or C3 runtime acceptance implied.')
    with (BASE/'results.json').open('x',encoding='utf8') as stream:json.dump(report,stream,indent=2)
    print(json.dumps(dict(completed=True,quality_passed=report['quality_passed'],
        languages=report['test']['quantized']['languages'],workspace=parity['workspace'],firmware_installed=False),indent=2))


if __name__=='__main__':main()
