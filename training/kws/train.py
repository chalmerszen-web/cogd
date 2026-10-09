"""Finite seeded streaming-event training. Test split is never read here."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import time
import numpy as np
import torch
from torch.nn import functional as F
from model import Model,checkpoint_channels

def masked_bce(output,target,weights=None,reduction='mean'):
    valid=target>=0
    loss=F.binary_cross_entropy_with_logits(output,target.clamp_min(0),pos_weight=torch.tensor(8.),reduction='none')
    loss=loss*valid
    if weights is not None:loss=loss*weights
    return loss.sum() if reduction=='sum' else loss.sum()/valid.sum().clamp_min(1)


def negative_peaks(output,target):
    """Per-clip loss for the three highest negative frames, plus valid mask.

    Short trigger peaks must not disappear in a 128-frame average. Entirely
    negative clips only: positive endpoint uncertainty remains unchanged.
    """
    negative=(target==0).flatten(1).all(1)
    loss=F.softplus(output).flatten(1).topk(3,dim=1).values.mean(1)
    return loss*negative,negative


def sampling_groups(data, paired=False, balanced=False, contrast=False):
    groups=[np.flatnonzero((data['label']==1)&(data['language']==language)) for language in ('zh','yue')]
    probabilities=[.25,.25,.5]
    groups.append(np.flatnonzero(data['label']==0))
    if balanced:
        groups=groups[:2]+[np.flatnonzero((data['label']==0)&(data['sampling_group']==kind)) for kind in ('hard_negative','natural_negative')]
        probabilities=[.25,.25,.35,.15]
    if paired:
        groups=groups[:2]+[np.flatnonzero((data['label']==0)&(data['sampling_group']==kind))
                          for kind in ('hard_negative','natural_negative','paired_negative')]
        probabilities=[.25,.25,.20,.10,.20]
    if contrast:
        if not paired:raise ValueError('Contrast sampling requires paired negatives')
        hard=(data['label']==0)&(data['sampling_group']=='hard_negative')
        near=data['text']=='你好小燕'
        groups=groups[:2]+[np.flatnonzero(hard&near),np.flatnonzero(hard&~near)]+groups[3:]
        probabilities=[.25,.25,.10,.10,.10,.20]
    if any(not len(g) for g in groups):raise ValueError('Missing positive language or negative group')
    return groups,probabilities


def focus_groups(data, groups, prefix):
    """Optional TRAIN-only emphasis; source identity and split never change."""
    if not prefix:return [np.array([],dtype=int) for _ in groups]
    selected=np.char.startswith(data['clip_id'].astype(str),prefix)
    if not selected.any():raise ValueError('Focus prefix matches no training examples')
    return [g[selected[g]] for g in groups]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--features',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--seed',type=int,default=20260921);ap.add_argument('--steps',type=int,default=1000)
    ap.add_argument('--batch',type=int,default=16);ap.add_argument('--threads',type=int,default=4)
    ap.add_argument('--channels',type=int,choices=(24,48),default=24)
    ap.add_argument('--learning-rate',type=float,default=.001)
    ap.add_argument('--boundary-negative-weight',type=float,default=1.)
    ap.add_argument('--balanced-hard-negatives',action='store_true')
    ap.add_argument('--initial-checkpoint',type=Path)
    ap.add_argument('--device-fraction',type=float,default=0.)
    ap.add_argument('--paired-negatives',action='store_true')
    ap.add_argument('--contrast-negatives',action='store_true')
    ap.add_argument('--negative-peak-weight',type=float,default=0.)
    ap.add_argument('--focus-prefix',help='Emphasize a diagnosed supplemental TRAIN source prefix within its existing class')
    ap.add_argument('--focus-fraction',type=float,default=0.)
    args=ap.parse_args()
    if not 1<=args.steps<=10000 or args.batch<1 or not 1<=args.boundary_negative_weight<=4 or not 0<=args.device_fraction<=.75:
        raise ValueError('Invalid bounded training budget')
    if not 0<=args.negative_peak_weight<=1:raise ValueError('Invalid negative peak weight')
    if not 0<=args.focus_fraction<=.5 or bool(args.focus_prefix)!=bool(args.focus_fraction):
        raise ValueError('Focus requires a source prefix and fraction in (0,.5]')
    args.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(args.threads);torch.manual_seed(args.seed);random.seed(args.seed);np.random.seed(args.seed)
    torch.use_deterministic_algorithms(True)
    rng=np.random.default_rng(args.seed)
    data=np.load(args.features/'train.npz',allow_pickle=False)
    val=np.load(args.features/'validation.npz',allow_pickle=False)
    x=torch.from_numpy(data['x'].astype(np.float32).transpose(0,2,1)/32)
    y=torch.from_numpy(data['y'][:,None,:]);vx=torch.from_numpy(val['x'].astype(np.float32).transpose(0,2,1)/32)
    vy=torch.from_numpy(val['y'][:,None,:])
    frame_end=(np.arange(data['x'].shape[1])+1)*256
    ends=data['event_end'][:,None]
    boundary=(ends>0)&(frame_end[None,:]<ends)&(frame_end[None,:]>=ends-2560)
    weights=torch.from_numpy(np.where(boundary,args.boundary_negative_weight,1.).astype(np.float32))[:,None,:]
    groups,probabilities=sampling_groups(data,args.paired_negatives,args.balanced_hard_negatives,args.contrast_negatives)
    focused=focus_groups(data,groups,args.focus_prefix)
    domain=data['device_domain'] if 'device_domain' in data else np.zeros(len(data['x']),dtype=bool)
    subgroups=[(g[~domain[g]],g[domain[g]]) for g in groups]
    if args.device_fraction and any(not len(a) or not len(b) for a,b in subgroups):
        raise ValueError('Device balancing requires original and captured members of every class')
    validation_weights=torch.ones(len(vx))
    if args.device_fraction:
        vd=val['device_domain'];fraction=float(vd.mean())
        if not 0<fraction<1:raise ValueError('Both validation domains are required')
        validation_weights=torch.from_numpy(np.where(vd,args.device_fraction/fraction,
            (1-args.device_fraction)/(1-fraction)).astype(np.float32))
    model=Model(args.channels)
    best=float('inf');start=time.monotonic();history=[]
    norm=json.loads((args.features/'normalization.json').read_text())
    if args.initial_checkpoint:
        initial=torch.load(args.initial_checkpoint,map_location='cpu',weights_only=True)
        if checkpoint_channels(initial)!=args.channels:raise ValueError('Fine-tuning requires identical model width')
        if initial['normalization']!=norm:raise ValueError('Fine-tuning requires identical frontend normalization')
        model.load_state_dict(initial['state_dict'])
    optimizer=torch.optim.AdamW(model.parameters(),lr=args.learning_rate,weight_decay=.0001)
    feature_hashes={name:hashlib.sha256((args.features/name).read_bytes()).hexdigest()
                    for name in ('train.npz','validation.npz','normalization.json','features.json')}
    snapshot=args.out/'training-source';snapshot.mkdir()
    source_hashes={}
    for path in sorted(Path(__file__).resolve().parent.glob('*')):
        if path.suffix in ('.py','.txt') and path.is_file():
            content=path.read_bytes();(snapshot/path.name).write_bytes(content)
            source_hashes[path.name]=hashlib.sha256(content).hexdigest()
    config={**vars(args),'features':str(args.features),'out':str(args.out), 'torch':str(torch.__version__),
            'initial_checkpoint':str(args.initial_checkpoint) if args.initial_checkpoint else None,
            'initial_checkpoint_sha256':hashlib.sha256(args.initial_checkpoint.read_bytes()).hexdigest() if args.initial_checkpoint else None,
            'feature_hashes':feature_hashes,'training_source_hashes':source_hashes}
    (args.out/'config.json').write_text(json.dumps(config,indent=2)+'\n')
    for step in range(1,args.steps+1):
        model.train()
        choices=rng.choice(len(groups),args.batch,p=probabilities)
        indices=[]
        for g in choices:
            group=subgroups[g][int(rng.random()<args.device_fraction)] if args.device_fraction else groups[g]
            if len(focused[g]) and rng.random()<args.focus_fraction:group=focused[g]
            indices.append(rng.choice(group))
        indices=np.asarray(indices)
        output=model(x[indices]);loss=masked_bce(output[:,:,128:],y[indices,:,128:],weights[indices,:,128:])
        if args.negative_peak_weight:
            peaks,negative=negative_peaks(output[:,:,128:],y[indices,:,128:])
            loss=loss+args.negative_peak_weight*peaks.sum()/negative.sum().clamp_min(1)
        optimizer.zero_grad();loss.backward();torch.nn.utils.clip_grad_norm_(model.parameters(),5.);optimizer.step()
        if step==1 or step%50==0 or step==args.steps:
            model.eval();total=0.;count=0;peak_total=0.;negative_count=0.
            with torch.no_grad():
                for offset in range(0,len(vx),args.batch):
                    output=model(vx[offset:offset+args.batch])[:,:,128:]
                    target=vy[offset:offset+args.batch,:,128:]
                    sample_weights=validation_weights[offset:offset+args.batch,None,None]
                    total+=masked_bce(output,target,weights=sample_weights,reduction='sum').item()
                    count+=((target>=0)*sample_weights).sum().item()
                    if args.negative_peak_weight:
                        peaks,negative=negative_peaks(output,target)
                        per_clip=sample_weights.flatten()
                        peak_total+=(peaks*per_clip).sum().item()
                        negative_count+=(negative*per_clip).sum().item()
            metric=total/count+args.negative_peak_weight*peak_total/max(1.,negative_count)
            row=dict(step=step,training_loss=loss.item(),validation_loss=metric,seconds=time.monotonic()-start)
            history.append(row);print(json.dumps(row),flush=True)
            payload=dict(state_dict=model.state_dict(),normalization=norm,config=config,step=step,validation_loss=metric)
            torch.save(payload,args.out/'last.pt')
            if metric<best:
                best=metric;torch.save(payload,args.out/'best.pt')
            (args.out/'history.json').write_text(json.dumps(history,indent=2)+'\n')
    (args.out/'checkpoint-sha256.txt').write_text(hashlib.sha256((args.out/'best.pt').read_bytes()).hexdigest()+'\n')

if __name__=='__main__': main()
