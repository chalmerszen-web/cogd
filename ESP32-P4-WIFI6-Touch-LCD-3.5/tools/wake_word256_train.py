"""Finite large-model experiment; TRAIN and TRAIN monitor only."""
import argparse,hashlib,json,os,random,sys,time
from pathlib import Path
import numpy as np
import torch
from torch.nn import functional as F
from wake_word256 import Model,initialize,score,CLASSES,CONTRACT
from wake_joint_train import event_loss,event_opportunities
from coverage_sampling import CoverageSampler

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def batch(d):
    classes=np.array([CLASSES.index(str(g)) if g!='negative' else 0 for g in d['wake_group']])
    return (torch.from_numpy(d['x'].astype(np.float32).transpose(0,2,1)/32),torch.from_numpy(d['y'][:,None]),
            torch.from_numpy(d['label']==1),torch.from_numpy(np.maximum(d['event_start_accept'],d['event_end']-1280)),
            torch.from_numpy(d['event_end']+6400),torch.full((len(classes),),d['x'].shape[1],dtype=torch.int64),torch.from_numpy(classes))

def objective(logits,values):
    target,positive,low,high,length,classes=values
    event,_=event_loss(score(logits),target,positive,low,high,length,margin=2.)
    ends=(torch.arange(logits.shape[-1],device=logits.device)+1)*256
    valid=ends[None]>=32768
    active=(ends[None]>=low[:,None])&(ends[None]<=high[:,None])&positive[:,None]
    labels=active.long()*classes[:,None]
    ce=(F.cross_entropy(logits,labels,reduction='none')*valid).sum()/(valid.sum()*len(logits))
    return event+.25*ce

def main():
    p=argparse.ArgumentParser();p.add_argument('--features',type=Path,required=True);p.add_argument('--initial',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--steps',type=int,default=4000);a=p.parse_args();assert 1000<=a.steps<=4000
    a.out.mkdir(parents=True,exist_ok=False);os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG',':4096:8')
    torch.set_num_threads(4);torch.use_deterministic_algorithms(True);torch.backends.cudnn.benchmark=False
    seed=2026110157;torch.manual_seed(seed);np.random.seed(seed);random.seed(seed)
    data=dict(np.load(a.features/'train.npz',allow_pickle=False));validation=dict(np.load(a.features/'validation.npz',allow_pickle=False))
    meta=json.loads((a.features/'features.json').read_text());assert meta['phonetic_complete_ablation'] and meta['no_dev_or_final_sources']
    norm=json.loads((a.features/'normalization.json').read_text());saved=torch.load(a.initial,map_location='cpu',weights_only=True);assert norm==saved['normalization']
    network=Model();initialize(network,saved);network=network.cuda();train=batch(data);monitor=batch(validation)
    tags=np.array([str(g)+('_live' if str(c).startswith('live-') else '_direct') for g,c in zip(data['wake_group'],data['clip_id'])])
    groups={str(g):np.flatnonzero(tags==g) for g in np.unique(tags)};quotas={g:max(12 if not g.startswith('negative') else 20,(len(v)+a.steps-1)//a.steps) for g,v in groups.items()}
    sampler=CoverageSampler(groups,quotas,steps=a.steps,batch=sum(quotas.values()),seed=seed,count=len(tags))
    cfg={'contract':CONTRACT,'channels':256,'hop_samples':512,'receptive_field_samples':32256,'classes':CLASSES,'steps':a.steps,'seed':seed,
         'parameters':sum(p.numel() for p in network.parameters()),'quotas':quotas,'scope':'TRAIN only; final excluded',
         'input_hashes':{str(p):sha(p) for p in [a.initial,*[a.features/n for n in ('train.npz','validation.npz','features.json','normalization.json')]]},
         'source_sha256':sha(__file__),'model_source_sha256':sha(Path(__file__).with_name('wake_word256.py')),
         'initialization':'96feature transfer;32ms hop and final depthwise kernel changed, no initial functional-parity claim'}
    (a.out/'config.json').write_text(json.dumps(cfg,indent=2),encoding='utf8');print(json.dumps(cfg),flush=True)
    optimizer=torch.optim.AdamW(network.parameters(),lr=.0002,weight_decay=.0001);scheduler=torch.optim.lr_scheduler.CosineAnnealingLR(optimizer,T_max=a.steps,eta_min=.00003)
    started=time.monotonic();best=float('inf');best_step=None;history=[]
    for step in range(1,a.steps+1):
        assert time.monotonic()-started<1800,'Finite30minute fit limit'
        idx=sampler.next();values=[x[idx].cuda() for x in train];network.train();logits=network(values[0]);cost=objective(logits,values[1:]);assert torch.isfinite(cost)
        optimizer.zero_grad();cost.backward();torch.nn.utils.clip_grad_norm_(network.parameters(),5.);optimizer.step();scheduler.step()
        if step==1 or step%400==0 or step==a.steps:
            network.eval();total=0.;stats={}
            with torch.no_grad():
                for begin in range(0,len(validation['x']),64):
                    values=[x[begin:begin+64].cuda() for x in monitor];out=network(values[0]);total+=float(objective(out,values[1:]))*len(out)
                    opportunities,end=event_opportunities(score(out));allowed=(end[None]>=values[3][:,None])&(end[None]<=values[4][:,None])&values[2][:,None]
                    valid=(end[None]>=32768)&(end[None]<=values[5][:,None]*256)
                    hit=((opportunities>268/256)&allowed).any(1).cpu().tolist();false=((opportunities>268/256)&valid&~allowed).any(1).cpu().tolist()
                    for i,(h,f) in enumerate(zip(hit,false)):
                        group=str(validation['wake_group'][begin+i]);g=stats.setdefault(group,{'count':0,'hits':0,'false':0});g['count']+=1;g['hits']+=h;g['false']+=f
            value=total/len(validation['x']);row={'step':step,'train_loss':float(cost.detach()),'monitor_loss':value,'groups':stats,'seconds':time.monotonic()-started};history.append(row);print(json.dumps(row),flush=True)
            state={'state_dict':{k:v.cpu().clone() for k,v in network.state_dict().items()},'normalization':norm,'config':cfg,'step':step};torch.save(state,a.out/'last.pt')
            if value<best:best=value;best_step=step;torch.save(state,a.out/'best.pt')
            (a.out/'history.json').write_text(json.dumps(history,indent=2),encoding='utf8')
    report={'complete':True,'best_step':best_step,'coverage':sampler.report(require_complete=True),'elapsed_seconds':time.monotonic()-started,'best_sha256':sha(a.out/'best.pt')}
    (a.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8');print(json.dumps(report),flush=True)

if __name__=='__main__':main()
