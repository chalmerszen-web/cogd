"""Finite TRAIN-only CTC96 experiment using audited Qwen pronunciations."""
import argparse,hashlib,json,os,random,re,sys,time
from pathlib import Path
import numpy as np
import torch
from wake_ctc96 import Model,TOKENS,PHRASES,CONTRACT,phonemes,loss,decode,collapse
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from coverage_sampling import CoverageSampler

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def select(data,sources,complete_only=False):
    ids=[];targets=[];tags=[];weights=[];excluded=0
    pattern=re.compile(r'wide95(?:b|-soft31)?-train-[A-Za-z0-9_.]+-\d{4}')
    for i,clip in enumerate(data['clip_id']):
        found=pattern.search(str(clip));source=sources.get(found.group(0)) if found else None
        positive=bool(data['label'][i]);group=str(data['wake_group'][i])
        if complete_only and (source is None or positive!=bool(source['label'])):excluded+=1;continue
        if positive and source is None:excluded+=1;continue
        target=phonemes(source['phonetic_tokens']) if source else [9]
        if positive:
            assert PHRASES.get(tuple(target))==group,(clip,target,group)
        elif source and source['label']:
            # A cut-off positive has no audited complete phone transcript.
            target=[9]
        elif tuple(target) in PHRASES:
            raise ValueError('Negative source contains a complete wake phrase: '+str(clip))
        domain='live' if str(clip).startswith('live-') else 'direct'
        tag=group+'_'+domain if positive else 'negative_'+('phonetic_'+domain if source and not source['label'] else 'unknown')
        ids.append(i);targets.append(target);tags.append(tag);weights.append(1. if tag!='negative_unknown' else .25)
    return np.array(ids),targets,np.array(tags),np.array(weights,dtype=np.float32),excluded

def main():
    p=argparse.ArgumentParser();p.add_argument('--features',type=Path,required=True);p.add_argument('--initial',type=Path,required=True)
    p.add_argument('--sources',type=Path,nargs='+',required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--steps',type=int,default=4000);p.add_argument('--device',choices=('cpu','cuda'),default='cuda');p.add_argument('--deadline',action='store_true');p.add_argument('--audited-complete-only',action='store_true');a=p.parse_args()
    assert 1000<=a.steps<=4000;a.out.mkdir(parents=True,exist_ok=False)
    os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG',':4096:8');torch.set_num_threads(4);torch.use_deterministic_algorithms(True)
    torch.backends.cudnn.benchmark=False;seed=2026110147;random.seed(seed);np.random.seed(seed);torch.manual_seed(seed)
    sources={}
    for path in a.sources:
        for line in path.read_text(encoding='utf8').splitlines():
            row=json.loads(line);assert row['split']=='train' and row.get('phonetic_tokens')
            assert row['clip_id'] not in sources;sources[row['clip_id']]=row
    raw=dict(np.load(a.features/'train.npz',allow_pickle=False));monitor=dict(np.load(a.features/'validation.npz',allow_pickle=False))
    ids,targets,tags,weights,excluded=select(raw,sources,a.audited_complete_only);vi,vt,vg,vw,ve=select(monitor,sources,a.audited_complete_only)
    norm=json.loads((a.features/'normalization.json').read_text());saved=torch.load(a.initial,map_location='cpu',weights_only=True)
    assert saved['normalization']==norm and saved['config']['channels']==96
    network=Model()
    if saved['config'].get('contract')==CONTRACT:network.load_state_dict(saved['state_dict'])
    else:
        initial={k:v for k,v in saved['state_dict'].items() if not k.startswith('layers.11.')}
        missing=network.load_state_dict(initial,strict=False);assert set(missing.missing_keys)=={'layers.11.conv.weight','layers.11.conv.bias'} and not missing.unexpected_keys
        with torch.no_grad():
            torch.nn.init.normal_(network.layers[-1].conv.weight,std=.01);network.layers[-1].conv.bias.zero_();network.layers[-1].conv.bias[0]=2
    network=network.to(a.device);train=torch.from_numpy(raw['x'][ids].astype(np.float32).transpose(0,2,1)/32)
    groups={str(t):np.flatnonzero(tags==t) for t in np.unique(tags)}
    quotas={t:max(12 if not t.startswith('negative') else 16,(len(g)+a.steps-1)//a.steps) for t,g in groups.items()}
    sampler=CoverageSampler(groups,quotas,steps=a.steps,batch=sum(quotas.values()),seed=seed,count=len(ids))
    rng=np.random.default_rng(seed);mi=np.concatenate([rng.choice(np.flatnonzero(vg==g),min(32,sum(vg==g)),replace=False) for g in np.unique(vg)])
    mx=torch.from_numpy(monitor['x'][vi[mi]].astype(np.float32).transpose(0,2,1)/32);mt=[vt[i] for i in mi];mw=torch.tensor(vw[mi])
    deadline=torch.from_numpy(np.where(raw['label'][ids],raw['event_end'][ids]+3200,raw['x'].shape[1]*256)) if a.deadline else None
    vd=torch.from_numpy(np.where(monitor['label'][vi[mi]],monitor['event_end'][vi[mi]]+3200,monitor['x'].shape[1]*256)) if a.deadline else None
    cfg={'contract':CONTRACT,'tokens':TOKENS,'channels':96,'steps':a.steps,'seed':seed,'deadline':a.deadline,'audited_complete_only':a.audited_complete_only,'scope':'TRAIN only; frozen wide95 final excluded',
         'input_hashes':{str(p):sha(p) for p in [a.initial,*a.sources,*[a.features/n for n in ('train.npz','validation.npz','normalization.json')]]},
         'audited_sources':len(sources),'train_rows':len(ids),'excluded_unverified_positive_rows':excluded,'monitor_rows':len(mi),'quotas':quotas,
         'phonetic_target_source':'audited TRAIN tokens; unknown/non-audited negatives use OTHER, never invented tones',
         'source_sha256':sha(__file__),'model_source_sha256':sha(Path(__file__).with_name('wake_ctc96.py'))}
    (a.out/'config.json').write_text(json.dumps(cfg,indent=2),encoding='utf8');print(json.dumps(cfg),flush=True)
    optimizer=torch.optim.AdamW(network.parameters(),lr=.0005,weight_decay=.0001)
    scheduler=torch.optim.lr_scheduler.CosineAnnealingLR(optimizer,T_max=a.steps,eta_min=.00005)
    started=time.monotonic();history=[];best=float('inf');best_step=None
    for step in range(1,a.steps+1):
        assert time.monotonic()-started<1200,'Finite20minute training bound'
        index=sampler.next();network.train();output=network(train[index].to(a.device))
        objective=loss(output,[targets[i] for i in index],torch.from_numpy(weights[index]),None if deadline is None else deadline[index]);assert torch.isfinite(objective)
        optimizer.zero_grad();objective.backward();torch.nn.utils.clip_grad_norm_(network.parameters(),5.);optimizer.step();scheduler.step()
        if step==1 or step%400==0 or step==a.steps:
            network.eval()
            with torch.no_grad():
                outputs=torch.cat([network(x.to(a.device)).cpu() for x in mx.split(32)])
                cost=float(loss(outputs,mt,mw,vd));logits=outputs[:,:,1::2].transpose(1,2).numpy()
            exact=sum(collapse(row[64:])==target for row,target in zip(logits,mt));metrics={}
            for n,row in enumerate(logits):
                source=int(vi[mi[n]]);positive=bool(monitor['label'][source]);group=str(monitor['wake_group'][source]);events=decode(row[64:],offset=32768)
                lo=int(monitor['event_start_accept'][source]);hi=int(monitor['event_end'][source])+6400
                valid=lambda event:positive and lo<=event[0]<=hi and event[1]==group
                m=metrics.setdefault(group,{'count':0,'hit':0,'false':0});m['count']+=1;m['hit']+=any(valid(e) for e in events);m['false']+=any(not valid(e) for e in events)
            row={'step':step,'loss':float(objective.detach()),'monitor_loss':cost,'exact_transcripts':exact,'monitor':metrics,'elapsed_seconds':time.monotonic()-started}
            history.append(row);print(json.dumps(row),flush=True)
            state={'state_dict':{k:v.cpu().clone() for k,v in network.state_dict().items()},'normalization':norm,'config':cfg,'step':step}
            torch.save(state,a.out/'last.pt')
            if cost<best:best=cost;best_step=step;torch.save(state,a.out/'best.pt')
            (a.out/'history.json').write_text(json.dumps(history,indent=2),encoding='utf8')
    coverage=sampler.report(require_complete=True)
    report={'complete':True,'best_step':best_step,'coverage':coverage,'elapsed_seconds':time.monotonic()-started,'best_sha256':sha(a.out/'best.pt')}
    (a.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8');print(json.dumps(report),flush=True)

if __name__=='__main__':main()
