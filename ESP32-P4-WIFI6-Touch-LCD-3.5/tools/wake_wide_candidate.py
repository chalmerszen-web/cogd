"""Install only an integer-parity-checked P4 48/48/48 development candidate."""
import argparse, hashlib, json, shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5'

def read(p):return json.loads(p.read_text(encoding='utf8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--training',type=Path,required=True)
    p.add_argument('--validation',type=Path,required=True);p.add_argument('--features',type=Path,required=True)
    p.add_argument('--sync-confirm',action='store_true');p.add_argument('--mega',action='store_true');a=p.parse_args()
    training=a.training.resolve();validation=read(a.validation);config=read(training/'config.json')
    report=read(training/'training-report.json');assert report['complete'] and config['p4_wide']
    assert validation['complete'] and all(r['exact'] for r in validation['parity'])
    assert validation['evaluation_scope']=='training_monitor'
    assert config['feature_hashes']=={n:sha(a.features/n) for n in config['feature_hashes']}
    policy=read(training/'runtime.json')
    assert policy['topology']==([96,96,48] if a.mega else [48,48,48])
    assert not a.mega or (a.sync_confirm and config['p4_mega'] and policy.get('private_kernel_dir'))
    for key,hkey in [('temporal_source','sha256'),('wide_runtime_source','wide_runtime_sha256'),('wide_header_source','wide_header_sha256')]:
        assert sha(ROOT/policy[key])==policy[hkey]
    assert bool(policy.get('temporal_header_source'))==a.sync_confirm
    if policy.get('temporal_header_source'):assert sha(ROOT/policy['temporal_header_source'])==policy['temporal_header_sha256']
    if policy.get('private_kernel_dir'):
        for name,digest in policy['private_kernel_hashes'].items():assert sha(ROOT/policy['private_kernel_dir']/name)==digest
    target=P/'models'/('wake96' if a.mega else 'wake48_sync' if a.sync_confirm else 'wake48');target.mkdir(exist_ok=True)
    for name in ('first.c','second.c','verifier.c','first.json','second.json','verifier.json','runtime.json'):
        shutil.copyfile(training/name,target/name)
    provenance={'status':'DEVELOPMENT CANDIDATE; acoustic acceptance pending','topology':policy['topology'],
        'training':training.relative_to(ROOT).as_posix(),'features':a.features.as_posix(),
        'training_report':report,'feature_hashes':config['feature_hashes'],'parity':validation['parity'],
        'training_monitor':validation['thresholds']['740']['groups'],
        'frozen_wide95_final_used_for_training':False,
        'previous_failed_rounds_reused_for_training':{'round1':report.get('round1_test_reused_for_training'),
                                                     'round2':report.get('round2_audio_used_for_training')},'runtime':policy}
    (target/'provenance.json').write_text(json.dumps(provenance,indent=2),encoding='utf8')
    print(json.dumps({'installed':str(training),'status':provenance['status']}))

if __name__=='__main__':main()
