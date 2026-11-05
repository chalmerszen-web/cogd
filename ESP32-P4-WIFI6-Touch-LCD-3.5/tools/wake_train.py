"""Finite P4-only transfer learning; four positive groups and hard negatives.

Selection uses development validation, never test voices. All legacy examples
remain in the bounded sampling schedule. Embedded topology/gating is unchanged.
"""
import argparse, hashlib, json, random, sys, time
from pathlib import Path
import numpy as np
import torch
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'training/kws')]
from model import Model
from event_loss import event_loss, event_opportunities
from coverage_sampling import CoverageSampler
from calibrate import calibrate, emit_c

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--features',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--steps',type=int,default=4000)
    ap.add_argument('--allow-missing-validation',nargs='*',choices=['hi_yue'],default=[])
    ap.add_argument('--initial-dir',type=Path);ap.add_argument('--hardening',action='store_true')
    ap.add_argument('--heads',nargs='+',choices=['first','second','verifier'],default=['first','second','verifier'])
    ap.add_argument('--development-rehearsal',action='store_true',help='Explicit final fit: development is TRAIN monitoring, not independent validation')
    ap.add_argument('--retire-test-round1',action='store_true',help='Explicitly consume failed round-one identities; round two must remain untouched')
    a=ap.parse_args();a.features=a.features.resolve();a.out=a.out.resolve()
    assert 1000<=a.steps<=6000;a.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(4);torch.use_deterministic_algorithms(True)
    data=dict(np.load(a.features/'train.npz',allow_pickle=False));val=dict(np.load(a.features/'validation.npz',allow_pickle=False))
    norm=json.loads((a.features/'normalization.json').read_text())
    metadata=json.loads((a.features/'features.json').read_text(encoding='utf8'))
    if a.development_rehearsal:assert metadata.get('development_reused_for_training') is True
    else:assert not set(data['source_group'])&set(val['source_group'])
    forbidden=('af_bella','am_fenrir','Kiki','bf_emma','bm_george','af_nicole','am_adam')
    if a.retire_test_round1:
        assert a.development_rehearsal and metadata.get('round1_test_reused_for_training') is True
        assert set(metadata['retired_test_source_groups'])=={'kokoro-v1-af_bella','kokoro-v1-am_fenrir','qwen3-system-Kiki'}
        forbidden=('bf_emma','bm_george','af_nicole','am_adam')
    for d in (data,val):assert not any(v in str(s) for s in d['source_group'] for v in forbidden)
    groups={};quotas={};tags=[]
    for i in range(len(data['x'])):
        if data['label'][i]:
            domain='p4' if data['p4_domain'][i] else 'new' if data['new_source'][i] else 'legacy'
            tag=str(data['wake_group'][i])+'_'+domain
        else:
            tag='negative_new' if data['new_source'][i] else str(data['sampling_group'][i])
            if a.hardening and data['p4_domain'][i]:tag='negative_p4'
            elif a.hardening and str(data['text'][i]).lower().startswith(('hi','嗨')):tag='negative_hi'
        tags.append(tag)
    tags=np.array(tags)
    for raw_tag in np.unique(tags):
        tag=str(raw_tag)
        groups[tag]=np.flatnonzero(tags==tag)
        quotas[tag]=5 if tag in ('hard_negative','natural_negative','paired_negative','negative_new') else 3 if tag.startswith('hi_') and tag.endswith('_new') else 2
        if a.hardening and (tag.startswith('negative_') or tag.endswith('_p4')):quotas[tag]=5
    batch=sum(quotas.values())
    x=torch.from_numpy(data['x'].astype(np.float32).transpose(0,2,1)/32);y=torch.from_numpy(data['y'][:,None])
    vx=torch.from_numpy(val['x'].astype(np.float32).transpose(0,2,1)/32);vy=torch.from_numpy(val['y'][:,None])
    positive=torch.from_numpy(data['label']==1);vpositive=torch.from_numpy(val['label']==1)
    low=torch.from_numpy(np.maximum(data['event_start_accept'],data['event_end']-1280));high=torch.from_numpy(data['event_end']+6400)
    vlow=torch.from_numpy(np.maximum(val['event_start_accept'],val['event_end']-1280));vhigh=torch.from_numpy(val['event_end']+6400)
    length=torch.full((len(x),),x.shape[-1],dtype=torch.int64);vlength=torch.full((len(vx),),vx.shape[-1],dtype=torch.int64)
    validation_groups={g:np.flatnonzero(val['wake_group']==g) for g in ('nihao_zh','nihao_yue','hi_zh','hi_yue','negative')}
    validation_groups['p4_positive']=np.flatnonzero(val['p4_domain']&(val['label']==1))
    missing=[g for g,ids in validation_groups.items() if not len(ids)]
    assert set(missing)<=set(a.allow_missing_validation),missing
    validation_groups={g:ids for g,ids in validation_groups.items() if len(ids)}
    native_health=['qwen3-system-Rocky']+(['qwen3-system-Kiki'] if a.retire_test_round1 else [])
    health_ids=np.flatnonzero(data['p4_domain'] | np.isin(data['source_group'],native_health))
    hashes={n:sha(a.features/n) for n in ('train.npz','validation.npz','normalization.json','features.json','channel.json')}
    configurations=[('first',24,'artifacts/kws-phase5/adapt-e/best.pt','kws_verified_primary'),
                    ('second',24,'artifacts/kws-bilingual/adapt-l/best.pt','kws_verified_secondary'),
                    ('verifier',48,'artifacts/voice-fast/wake-anchor-fit-ux274/final.pt','kws_trained_model')]
    results=[]
    for index,(name,channels,checkpoint,symbol) in enumerate(configurations):
        if name not in a.heads:continue
        if a.initial_dir:checkpoint=(a.initial_dir.resolve()/name/'best.pt').relative_to(ROOT).as_posix()
        out=a.out/name;out.mkdir();seed=2026110101+index*101
        random.seed(seed);np.random.seed(seed);torch.manual_seed(seed)
        model=Model(channels);old=torch.load(ROOT/checkpoint,map_location='cpu',weights_only=True)
        assert old['normalization']==norm;model.load_state_dict(old['state_dict'])
        learning_rate=.0001 if a.hardening else .0003
        margin=2.0 if a.hardening else 1.5
        optimizer=torch.optim.AdamW(model.parameters(),lr=learning_rate,weight_decay=.0001)
        schedule=torch.optim.lr_scheduler.CosineAnnealingLR(optimizer,T_max=a.steps,eta_min=.00003)
        sampler=CoverageSampler(groups,quotas,steps=a.steps,batch=batch,seed=seed,count=len(x))
        cfg={'channels':channels,'seed':seed,'steps':a.steps,'batch':batch,'feature_hashes':hashes,'quotas':quotas,
             'initial':checkpoint,'initial_sha256':sha(ROOT/checkpoint),'learning_rate':learning_rate,'minimum_learning_rate':.00003,
             'positive_margin':margin,'hardening':a.hardening,'test_read':a.retire_test_round1,'current_test_read':False,'missing_validation_groups':missing,
             'evaluation_scope':'training_monitor' if a.development_rehearsal else 'source_disjoint_development',
             'round1_test_reused_for_training':a.retire_test_round1,'forbidden_test_voices':list(forbidden),
             'source_sha256':sha(Path(__file__))}
        (out/'config.json').write_text(json.dumps(cfg,indent=2),encoding='utf8')
        began=time.monotonic();best=float('inf');history=[]
        for step in range(1,a.steps+1):
            if time.monotonic()-began>2400:raise TimeoutError('40-minute per-head training limit')
            indices=sampler.next();model.train();output=model(x[indices])
            loss,components=event_loss(output,y[indices],positive[indices],low[indices],high[indices],length[indices],margin)
            assert torch.isfinite(loss)
            optimizer.zero_grad();loss.backward();torch.nn.utils.clip_grad_norm_(model.parameters(),5);optimizer.step();schedule.step()
            if step==1 or step%400==0 or step==a.steps:
                model.eval();metrics={};objective=0.
                with torch.no_grad():
                    for group,ids in validation_groups.items():
                        total=0.;hits=0;false_events=0
                        for off in range(0,len(ids),64):
                            selected=ids[off:off+64];logits=model(vx[selected])
                            penalty,_=event_loss(logits,vy[selected],vpositive[selected],vlow[selected],vhigh[selected],vlength[selected],1.5)
                            total+=float(penalty)*len(selected)
                            opportunity,ends=event_opportunities(logits)
                            valid=ends[None]>=32768
                            accepted=valid&vpositive[selected,None]&(ends[None]>=vlow[selected,None])&(ends[None]<=vhigh[selected,None])
                            hits+=int(((opportunity>=268/256)&accepted).any(1).sum())
                            false_events+=int(((opportunity>=268/256)&valid&~accepted).any(1).sum())
                        metrics[group]={'clips':len(ids),'hits':hits,'forbidden':false_events,'loss':total/len(ids)}
                        weight=.7 if a.hardening and group=='negative' else .2 if group in ('negative','p4_positive') else .15
                        objective+=weight*total/len(ids)
                health={};eligible=True
                if a.hardening:
                    hp=hh=hn=hf=0
                    with torch.no_grad():
                        for off in range(0,len(health_ids),64):
                            selected=health_ids[off:off+64];opportunity,ends=event_opportunities(model(x[selected]))
                            valid=ends[None]>=32768
                            accepted=valid&positive[selected,None]&(ends[None]>=low[selected,None])&(ends[None]<=high[selected,None])
                            hp+=int(positive[selected].sum());hn+=int((~positive[selected]).sum())
                            hh+=int(((opportunity>=268/256+.5)&accepted).any(1).sum())
                            hf+=int(((opportunity>=268/256)&valid&~positive[selected,None]).any(1).sum())
                    eligible=hp>0 and hh/hp>=.98 and hf<=max(1,int(hn*.02))
                    health={'training_only':True,'positive':hp,'hits_with_extra_margin':hh,'negative':hn,'false':hf,'eligible':eligible}
                row={'head':name,'step':step,'train_loss':float(loss),'validation_objective':objective,'groups':metrics,'training_health':health,'seconds':time.monotonic()-began}
                history.append(row);print(json.dumps(row),flush=True)
                payload={'state_dict':model.state_dict(),'normalization':norm,'config':cfg,'step':step,'validation_loss':objective}
                if eligible and objective<best:
                    best=objective;torch.save(payload,out/'best.pt')
                (out/'history.json').write_text(json.dumps(history,indent=2),encoding='utf8')
        torch.save(payload,out/'last.pt')
        coverage=sampler.report(require_complete=True);(out/'coverage.json').write_text(json.dumps(coverage,indent=2),encoding='utf8')
        assert (out/'best.pt').exists(),'No checkpoint met the training-fit and development-selection gate'
        bundle=calibrate(out/'best.pt',a.features,max_clips=512)
        bundle['name']=f'xiaoyan_p4_wake4_c{channels}_v1'
        (a.out/(name+'.json')).write_text(json.dumps(bundle,separators=(',',':')),encoding='utf8')
        (a.out/(name+'.c')).write_text(emit_c(bundle,symbol),encoding='utf8',newline='\n')
        results.append({'head':name,'best_step':bundle['training_step'],'seconds':time.monotonic()-began,'checkpoint_sha256':sha(out/'best.pt'),
                        'model_c_sha256':sha(a.out/(name+'.c')),'validation_objective':best})
        (a.out/'training-report.json').write_text(json.dumps({'complete':len(results)==len(set(a.heads)),'requested_heads':a.heads,'heads':results,'no_test_read':not a.retire_test_round1,'current_test_read':False,'round1_test_reused_for_training':a.retire_test_round1,'evaluation_scope':cfg['evaluation_scope']},indent=2),encoding='utf8')
    print(json.dumps({'complete':True,'heads':results}),flush=True)

if __name__=='__main__':main()
