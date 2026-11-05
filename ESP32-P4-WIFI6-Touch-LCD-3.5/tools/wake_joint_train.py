"""Fit all three fixed-size heads against their combined confirmation score.

Only TRAIN features are consumed. Round-one identities were explicitly retired
into TRAIN; round-two failures motivated this objective, but their recordings
and the reserved bf_emma/bm_george/af_nicole/am_adam voices are excluded.
The joint objective conservatively asks primary and verifier to agree in the
same two-of-three window; the device still permits its bounded pending window.
"""
import argparse, hashlib, itertools, json, os, random, sys, time
from pathlib import Path
import numpy as np
import torch
from torch.nn import functional as F

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
if '--mega' in sys.argv:sys.path.insert(0,str(Path(__file__).resolve().parent/'kws96_training'))
from model import Model
from event_loss import decision_scores
from coverage_sampling import CoverageSampler
from calibrate import calibrate,emit_c
POS_MARGIN=2.
NEGATIVE_WEIGHT=1.

def spectral_augment(x,norm):
    """TRAIN-only smooth vocal-tract/EQ perturbation; timestamps stay exact.

    Undo the per-band normalization before interpolation. A quarter of each
    batch stays unchanged. This is feature augmentation, not extra speakers.
    """
    with torch.no_grad():
        n,c,t=x.shape
        mean=x.new_tensor(norm['mean_q8'])[None,:,None]/256
        inv=x.new_tensor(norm['inverse_std_q12'])[None,:,None]/4096
        raw=x/inv+mean
        ratio=.94+.12*torch.rand(n,1,1,device=x.device)
        position=((torch.arange(c,device=x.device)[None,:,None]+1)/ratio-1).clamp(0,c-1)
        lo=position.floor().long();hi=(lo+1).clamp_max(c-1);fraction=position-lo
        warped=raw.gather(1,lo.expand(n,c,t))*(1-fraction)+raw.gather(1,hi.expand(n,c,t))*fraction
        eq=F.interpolate(torch.rand(n,1,5,device=x.device)*1.5-.75,size=c,mode='linear',align_corners=True).transpose(1,2)
        gain=torch.rand(n,1,1,device=x.device)-.5
        result=((warped+eq+gain-mean)*inv).clamp(-4,127/32)
        keep=torch.rand(n,1,1,device=x.device)<.25
        return torch.where(keep,x,result)

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def event_opportunities(logits):
    # torch.median(..., dim=...) has no deterministic CUDA implementation in
    # the installed Torch. A three-value sorting network has the same exact
    # forward median and deterministic, finite gradients, including ties.
    scores=decision_scores(logits);windows=scores.unfold(1,3,1)
    a,b,c=windows.unbind(-1)
    median=torch.maximum(torch.minimum(a,b),torch.minimum(torch.maximum(a,b),c))
    end=(torch.arange(median.shape[1],device=logits.device)+3)*512
    return median/256,end

def event_loss(logits,target,positive,low,high,length,margin=1.):
    # P4-local equivalent of the shared per-head event objective; keep C3's
    # implementation unchanged while using the deterministic median above.
    _,_,frames=logits.shape
    score,end=event_opportunities(logits)
    valid=(end[None]>=32768)&(end[None]<=length[:,None]*256)
    accepted=valid&positive[:,None]&(end[None]>=low[:,None])&(end[None]<=high[:,None])
    forbidden=valid&~accepted
    pos_rows=accepted.any(1);neg_rows=forbidden.any(1)
    best=score.masked_fill(~accepted,-65536).max(1).values
    worst=score.masked_fill(~forbidden,-65536).max(1).values
    pos=(F.relu(268/256+margin-best).square()*pos_rows).sum()/pos_rows.sum().clamp_min(1)
    neg=(F.relu(worst-(268/256-margin)).square()*neg_rows).sum()/neg_rows.sum().clamp_min(1)
    frame_end=(torch.arange(frames,device=logits.device)+1)*256
    frame_valid=(frame_end[None]>=32768)&(frame_end[None]<=length[:,None]*256)&(target[:,0]>=0)
    frame_accepted=positive[:,None]&(frame_end[None]>=low[:,None])&(frame_end[None]<=high[:,None])
    aligned=frame_valid&((target[:,0]>0)==frame_accepted)
    point=(F.binary_cross_entropy_with_logits(logits[:,0],target[:,0].clamp_min(0),reduction='none')*aligned).sum()/aligned.sum().clamp_min(1)
    return pos+neg+.25*point,None

def fuse(first,second):
    def rounded(x):
        q=x*256; integer=(q.sign()*(q.abs()+.5).floor()).clamp(-32768,32767)
        return q+(integer-q).detach()
    mean=(rounded(first)+rounded(second))/2
    return (mean+(mean.trunc()-mean).detach())/256

def opportunities(outputs):
    primary=fuse(outputs[0],outputs[1])
    p,end=event_opportunities(primary);v,vend=event_opportunities(outputs[2])
    assert torch.equal(end,vend)
    return torch.minimum(p,v),end,primary

def joint_loss(outputs,target,positive,low,high,length,margin=None):
    if margin is None:margin=POS_MARGIN
    score,end,primary=opportunities(outputs)
    valid=(end[None]>=32768)&(end[None]<=length[:,None]*256)
    allowed=valid&positive[:,None]&(end[None]>=low[:,None])&(end[None]<=high[:,None])
    forbidden=valid&~allowed
    best=score.masked_fill(~allowed,-65536).max(1).values
    worst=score.masked_fill(~forbidden,-65536).max(1).values
    has_positive=allowed.any(1);has_negative=forbidden.any(1)
    pos=(F.relu(268/256+margin-best).square()*has_positive).sum()/has_positive.sum().clamp_min(1)
    neg=(F.relu(worst-(268/256-1.5)).square()*has_negative).sum()/has_negative.sum().clamp_min(1)
    auxiliary=0.
    for output,weight in ((outputs[0],.2),(outputs[1],.2),(primary,.5),(outputs[2],.5)):
        loss,_=event_loss(output,target,positive,low,high,length,margin)
        auxiliary=auxiliary+weight*loss
    return pos+NEGATIVE_WEIGHT*neg+auxiliary,score,allowed,forbidden

def main():
    global POS_MARGIN,NEGATIVE_WEIGHT
    ap=argparse.ArgumentParser();ap.add_argument('--features',type=Path,required=True)
    ap.add_argument('--initial',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--steps',type=int,default=4000)
    ap.add_argument('--device',choices=('cpu','cuda'),default='cpu')
    ap.add_argument('--wide',action='store_true',help='P4-only 48/48/48 heads, initialized from explicit widened checkpoints')
    ap.add_argument('--mega',action='store_true',help='P4-only96/96/48 private kernel')
    ap.add_argument('--near-negative-focus',action='store_true')
    ap.add_argument('--spectral-augment',action='store_true')
    ap.add_argument('--ns-focus',action='store_true',help='Separate quotas for actual P4 post-NS TRAIN inputs')
    ap.add_argument('--stress-round',type=int,choices=(0,1,2,3),default=0)
    ap.add_argument('--retire-round2',action='store_true',help='Explicitly consume failed round-two identities, keeping round three untouched')
    a=ap.parse_args()
    assert not a.mega or a.wide
    assert not a.spectral_augment or a.mega
    POS_MARGIN=2.5 if a.stress_round>=2 else 2.
    NEGATIVE_WEIGHT=1.5 if a.stress_round==3 else 1.
    assert 1000<=a.steps<=6000
    a.features=a.features.resolve();a.initial=a.initial.resolve();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(4);torch.use_deterministic_algorithms(True)
    os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG',':4096:8')
    device=torch.device(a.device)
    if device.type=='cuda':
        assert torch.cuda.is_available()
        torch.backends.cudnn.benchmark=False
    seed=2026110119;random.seed(seed);np.random.seed(seed);torch.manual_seed(seed)
    data=dict(np.load(a.features/'train.npz',allow_pickle=False));val=dict(np.load(a.features/'validation.npz',allow_pickle=False))
    norm=json.loads((a.features/'normalization.json').read_text(encoding='utf8'))
    metadata=json.loads((a.features/'features.json').read_text(encoding='utf8'))
    if a.retire_round2:assert metadata.get('round2_audio_used_for_training') is True
    else:assert metadata.get('round2_audio_used_for_training') is not True
    forbidden=('af_nicole','am_adam') if a.retire_round2 else ('bf_emma','bm_george','af_nicole','am_adam')
    for d in (data,val):
        assert not any(word in str(s) for s in d['source_group'] for word in forbidden)
        assert not any('-r3-' in str(s) or (not a.retire_round2 and '-r2-' in str(s)) for s in d['clip_id'])
    tags=[]
    for i in range(len(data['x'])):
        ns=a.ns_focus and 'live-live-ns-train-' in str(data['clip_id'][i])
        weak=a.stress_round and 'live-live-low-010-' in str(data['clip_id'][i])
        fresh=a.stress_round and str(data['source_group'][i]).startswith('qwen31-system-')
        if a.wide:
            weak=weak or ns or any(tag in str(data['clip_id'][i]) for tag in ('live-live-a-','live-live-b-','live-live-c2-'))
            fresh=fresh or any(tag in str(data['clip_id'][i]) for tag in ('wide95-','wide95b-'))
        if data['label'][i]:
            domain='weak' if weak else 'fresh' if fresh else 'native' if str(data['source_group'][i]).startswith('qwen3-system-') else 'p4' if data['p4_domain'][i] else 'new' if data['new_source'][i] else 'legacy'
            tag=str(data['wake_group'][i])+'_'+domain
        else:
            tag='negative_weak' if weak else 'negative_fresh' if fresh else 'negative_p4' if data['p4_domain'][i] else 'negative_hi' if str(data['text'][i]).lower().startswith(('hi','嗨')) else 'negative_new' if data['new_source'][i] else str(data['sampling_group'][i])
            if a.near_negative_focus and (weak or fresh):
                text=str(data['text'][i])
                if any(name in text for name in ('小燕','小杨','小王','小云')):tag='negative_near_'+('weak' if weak else 'fresh')
                elif text.strip('。！! ,，').lower() in ('小言','hi','嗨','你好'):tag='negative_short_'+('weak' if weak else 'fresh')
        tags.append(tag+'_ns' if ns else tag)
    tags=np.array(tags);groups={str(t):np.flatnonzero(tags==t) for t in np.unique(tags)}
    quotas={t:5 if t.startswith('negative') or t in ('hard_negative','natural_negative','paired_negative') or t.endswith(('_native','_p4')) else 3 for t in groups}
    if a.stress_round:
        for tag in quotas:
            if tag.endswith(('_weak','_fresh')):quotas[tag]=8
    if a.ns_focus:
        assert any(tag.endswith('_ns') for tag in quotas)
        for tag in quotas:
            quotas[tag]=12 if tag.endswith('_ns') else max(2,(quotas[tag]+1)//2,(len(groups[tag])+a.steps-1)//a.steps)
    sampler=CoverageSampler(groups,quotas,steps=a.steps,batch=sum(quotas.values()),seed=seed,count=len(tags))
    def tensors(d):
        return (torch.from_numpy(d['x'].astype(np.float32).transpose(0,2,1)/32),
                torch.from_numpy(d['y'][:,None]),torch.from_numpy(d['label']==1),
                torch.from_numpy(np.maximum(d['event_start_accept'],d['event_end']-1280)),
                torch.from_numpy(d['event_end']+6400),torch.full((len(d['x']),),d['x'].shape[1],dtype=torch.int64))
    train=tensors(data);monitor=tensors(val)
    names=('first','second','verifier');widths=(24,24,48)
    if a.wide:widths=(48,48,48)
    if a.mega:widths=(96,96,48)
    models=[];initials={}
    for name,width in zip(names,widths):
        path=a.initial/name/'best.pt';saved=torch.load(path,map_location='cpu',weights_only=True)
        assert saved['normalization']==norm
        model=Model(width);model.load_state_dict(saved['state_dict']);models.append(model.to(device))
        initials[name]={'path':path.relative_to(ROOT).as_posix(),'sha256':sha(path)}
    cfg={'method':'joint primary-average and verifier two-of-three confirmation',
         'features':a.features.relative_to(ROOT).as_posix(),'feature_hashes':{n:sha(a.features/n) for n in ('train.npz','validation.npz','features.json','normalization.json')},
         'initials':initials,'steps':a.steps,'seed':seed,'quotas':quotas,'learning_rate':.00015,
         'evaluation_scope':'training_monitor','round1_test_reused_for_training':True,
         'round2_used_for_objective_diagnosis':True,'round2_audio_used_for_training':a.retire_round2,'training_device':a.device,
         'round3_audio_read':False,'forbidden_test_voices':list(forbidden),'source_sha256':sha(Path(__file__))}
    cfg.update(stress_round=a.stress_round,positive_margin=POS_MARGIN,negative_weight=NEGATIVE_WEIGHT,
               p4_wide=a.wide,
               p4_mega=a.mega,near_negative_focus=a.near_negative_focus,
               ns_focus=a.ns_focus,
               spectral_augmentation={'enabled':a.spectral_augment,'mel_axis_scale':[.94,1.06],'eq_log2_range':[-.75,.75],'gain_log2_range':[-.5,.5],'unchanged_probability':.25,'training_only':True},
               health_scope='unaugmented TRAIN capture/native/new-voice sources' if a.stress_round else 'all P4/native TRAIN variants')
    (a.out/'config.json').write_text(json.dumps(cfg,indent=2),encoding='utf8')
    optimizer=torch.optim.AdamW(itertools.chain(*(m.parameters() for m in models)),lr=.00015,weight_decay=.0001)
    scheduler=torch.optim.lr_scheduler.CosineAnnealingLR(optimizer,T_max=a.steps,eta_min=.00003)
    validation_groups={str(g):np.flatnonzero(val['wake_group']==g) for g in np.unique(val['wake_group'])}
    validation_groups['p4_positive']=np.flatnonzero(val['p4_domain']&(val['label']==1))
    health_ids=np.flatnonzero(data['p4_domain']|np.isin(data['source_group'],['qwen3-system-Rocky','qwen3-system-Kiki']))
    if a.stress_round:
        health_ids=np.flatnonzero((data['p4_domain']|np.array([str(s).startswith(('qwen3-system-','qwen31-system-')) for s in data['source_group']]))&(data['variant']==0))
        validation_groups['weak_capture']=np.flatnonzero(np.array(['live-live-low-010-' in str(c) for c in val['clip_id']]))
    best=float('inf');history=[];started=time.monotonic();best_step=None
    for step in range(1,a.steps+1):
        assert time.monotonic()-started<2400,'Finite forty-minute joint-training bound exceeded'
        selected=sampler.next();batch=[x[selected].to(device) for x in train]
        if a.spectral_augment:batch[0]=spectral_augment(batch[0],norm)
        for model in models:model.train()
        outputs=[model(batch[0]) for model in models]
        loss,_,_,_=joint_loss(outputs,*batch[1:]);assert torch.isfinite(loss)
        optimizer.zero_grad();loss.backward();torch.nn.utils.clip_grad_norm_(itertools.chain(*(m.parameters() for m in models)),5);optimizer.step();scheduler.step()
        if step!=1 and step%400 and step!=a.steps:continue
        for model in models:model.eval()
        metrics={};objective=0.
        with torch.no_grad():
            for group,ids in validation_groups.items():
                if not len(ids):continue
                total=hits=false=0
                for off in range(0,len(ids),48):
                    batch=[x[ids[off:off+48]].to(device) for x in monitor]
                    penalty,score,accepted,prohibited=joint_loss([m(batch[0]) for m in models],*batch[1:])
                    total+=float(penalty)*len(batch[0]);hits+=int(((score>=268/256)&accepted).any(1).sum())
                    false+=int(((score>=268/256)&prohibited).any(1).sum())
                metrics[group]={'clips':len(ids),'hits':hits,'forbidden':false,'loss':total/len(ids)}
                objective+=(.7 if group=='negative' else .15)*total/len(ids)
            hp=hh=hn=hf=0
            for off in range(0,len(health_ids),48):
                batch=[x[health_ids[off:off+48]].to(device) for x in train]
                _,score,accepted,prohibited=joint_loss([m(batch[0]) for m in models],*batch[1:])
                hp+=int(batch[2].sum());hn+=int((~batch[2]).sum())
                hh+=int(((score>=268/256+.5)&accepted).any(1).sum())
                hf+=int(((score>=268/256)&prohibited&~batch[2][:,None]).any(1).sum())
        eligible=hp>0 and hh/hp>=.98 and hf<=max(1,int(hn*.02))
        row={'step':step,'train_loss':float(loss),'objective':objective,'groups':metrics,
             'training_health':{'positive':hp,'hits_with_margin':hh,'negative':hn,'false':hf,'eligible':eligible},'seconds':time.monotonic()-started}
        history.append(row);print(json.dumps(row),flush=True)
        (a.out/'history.json').write_text(json.dumps(history,indent=2),encoding='utf8')
        if eligible and objective<best:
            best=objective;best_step=step
            for name,width,model in zip(names,widths,models):
                folder=a.out/name;folder.mkdir(exist_ok=True)
                config=dict(cfg,channels=width,initial=initials[name]['path'],initial_sha256=initials[name]['sha256'])
                torch.save({'state_dict':model.state_dict(),'normalization':norm,'config':config,'step':step,'validation_loss':objective},folder/'best.pt')
    (a.out/'coverage.json').write_text(json.dumps(sampler.report(require_complete=True),indent=2),encoding='utf8')
    assert best_step is not None,'No checkpoint passed the joint TRAIN health gate'
    results=[]
    for name,width,symbol in zip(names,widths,('kws_verified_primary','kws_verified_secondary','kws_trained_model')):
        bundle=calibrate(a.out/name/'best.pt',a.features,max_clips=512);bundle['name']=f'xiaoyan_p4_wake4_c{width}_'+('wide1' if a.wide else 'v1')
        (a.out/(name+'.json')).write_text(json.dumps(bundle,separators=(',',':')),encoding='utf8')
        (a.out/(name+'.c')).write_text(emit_c(bundle,symbol),encoding='utf8',newline='\n')
        results.append({'head':name,'best_step':best_step,'checkpoint_sha256':sha(a.out/name/'best.pt'),'model_c_sha256':sha(a.out/(name+'.c'))})
    report={'complete':True,'method':cfg['method'],'heads':results,'seconds':time.monotonic()-started,'evaluation_scope':'training_monitor','round1_test_reused_for_training':True,'round2_audio_used_for_training':a.retire_round2,'round3_audio_read':False,'training_device':a.device}
    (a.out/'training-report.json').write_text(json.dumps(report,indent=2),encoding='utf8');print(json.dumps(report),flush=True)

if __name__=='__main__':main()
