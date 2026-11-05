"""Install an integer-validated development candidate; never label it accepted."""
import argparse,hashlib,json,shutil
from pathlib import Path
P=Path(__file__).resolve().parents[1];ROOT=P.parent;B=P/'wake_training/20261101/three-rounds'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--round',type=int,choices=(1,2,3),required=True);a=p.parse_args()
    training=B/f'train-r{a.round}';validation=read(B/f'validation-r{a.round}/validation.json')
    assert read(training/'training-report.json')['complete'] and validation['complete'] and all(x['exact'] for x in validation['parity'])
    assert read(training/'config.json')['stress_round']==a.round
    assert read(B/'baseline-stress/report.json')['complete'],'Never change build during baseline acoustic playback'
    target=P/'models/wake4';previous=read(target/'provenance.json')
    for name in ('first.c','second.c','verifier.c','first.json','second.json','verifier.json','runtime.json'):shutil.copy2(training/name,target/name)
    provenance=dict(status='DEVELOPMENT CANDIDATE: acoustic and dialogue checks pending',stress_round=a.round,
        training=training.relative_to(P).as_posix(),features=(B/f'features-r{a.round}').relative_to(P).as_posix(),
        parent_status=previous['status'],historical_round1_and_round2_reused_for_training=True,
        historical_round3_used_for_training=False,new_test_used_for_training=False,
        evaluation_scope='training_monitor',parity=validation['parity'],quantized_training_monitor=validation['thresholds']['740']['groups'],
        training_report=read(training/'training-report.json'),runtime=read(target/'runtime.json'))
    (target/'provenance.json').write_text(json.dumps(provenance,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps({'installed_round':a.round,'monitor':provenance['quantized_training_monitor']}))
if __name__=='__main__':main()
