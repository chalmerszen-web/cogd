"""Explicitly export the three hardening heads for JOINT candidate validation.

The second head missed the original individual 2% TRAIN false-event gate by
one example (9/441). It is not marked as passing that gate: the complete
ensemble must still pass physical negatives and heldout acceptance.
"""
import argparse,hashlib,json,shutil,sys
from pathlib import Path
import torch
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'training/kws')]
from calibrate import calibrate,emit_c

def main():
    p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    b=a.base.resolve();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False);torch.set_num_threads(4)
    history=json.loads((b/'train-v4/second/history.json').read_text());last=history[-1];health=last['training_health']
    assert last['step']==2000 and health['hits_with_extra_margin']/health['positive']>=.98
    assert health['false']/health['negative']<=.03
    assert not health['eligible'],'This review is only for the retained non-passing individual checkpoint'
    sources={'first':b/'train-v4/first/best.pt','second':b/'train-v4/second/last.pt',
             'verifier':b/'train-v4-verifier/verifier/best.pt'}
    models=[]
    for name,symbol,width in [('first','kws_verified_primary',24),('second','kws_verified_secondary',24),('verifier','kws_trained_model',48)]:
        bundle=calibrate(sources[name],b/'features-v5',max_clips=512)
        bundle['name']=f'xiaoyan_p4_wake4_c{width}_v1'
        (a.out/(name+'.json')).write_text(json.dumps(bundle,separators=(',',':')),encoding='utf8')
        (a.out/(name+'.c')).write_text(emit_c(bundle,symbol),encoding='utf8',newline='\n')
        models.append({'head':name,'checkpoint':sources[name].relative_to(ROOT).as_posix(),
                       'checkpoint_sha256':hashlib.sha256(sources[name].read_bytes()).hexdigest(),'step':bundle['training_step']})
    report={'complete':True,'candidate_only':True,'models':models,'no_test_read':True,
        'second_original_training_gate':health,'second_joint_candidate_limit':.03,
        'note':'Original individual gate failure is retained. Export is for joint validation, not an acceptance result.'}
    (a.out/'training-report.json').write_text(json.dumps(report,indent=2),encoding='utf8');print(json.dumps(report))

if __name__=='__main__':main()
