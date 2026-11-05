"""Recover completed checkpoints after a reporting-only CoverageSampler error.

No optimizer step, checkpoint selection or training data is changed. The original
source and monitor history remain immutable. This corrects the monitor's omitted
2.048s warm-up exclusion and validates every promised sampling pass independently.
"""
import argparse,json,sys,time
from pathlib import Path
import numpy as np
import torch
from wake_ctc_train import select,sha,CoverageSampler
from wake_ctc96 import Model,decode,loss,collapse

def main():
    p=argparse.ArgumentParser();p.add_argument('--run',type=Path,required=True);a=p.parse_args();torch.set_num_threads(4)
    cfg=json.loads((a.run/'config.json').read_text());history=json.loads((a.run/'history.json').read_text())
    assert history[-1]['step']==cfg['steps'] and not (a.run/'report.json').exists()
    assert sha(a.run/'train-source.py')==cfg['source_sha256']
    sources={};data=None;monitor=None
    for name,digest in cfg['input_hashes'].items():
        path=Path(name);assert sha(path)==digest
        if path.name=='train.npz':data=dict(np.load(path,allow_pickle=False))
        elif path.name=='validation.npz':monitor=dict(np.load(path,allow_pickle=False))
        elif path.suffix=='.jsonl':
            for line in path.read_text(encoding='utf8').splitlines():
                row=json.loads(line);assert row['split']=='train';sources[row['clip_id']]=row
    ids,targets,tags,weights,excluded=select(data,sources)
    groups={str(t):np.flatnonzero(tags==t) for t in np.unique(tags)}
    sampler=CoverageSampler(groups,cfg['quotas'],steps=cfg['steps'],batch=sum(cfg['quotas'].values()),seed=cfg['seed'],count=len(ids))
    for _ in range(cfg['steps']):sampler.next()
    coverage=sampler.report(require_complete=True)
    best=torch.load(a.run/'best.pt',map_location='cpu',weights_only=True);last=torch.load(a.run/'last.pt',map_location='cpu',weights_only=True)
    expected=min(history,key=lambda row:row['monitor_loss']);assert last['step']==cfg['steps'] and best['step']==expected['step']
    network=Model().eval();network.load_state_dict(best['state_dict'])
    vi,vt,vg,vw,_=select(monitor,sources);rng=np.random.default_rng(cfg['seed'])
    mi=np.concatenate([rng.choice(np.flatnonzero(vg==g),min(32,sum(vg==g)),replace=False) for g in np.unique(vg)])
    mx=torch.from_numpy(monitor['x'][vi[mi]].astype(np.float32).transpose(0,2,1)/32);mt=[vt[i] for i in mi]
    with torch.no_grad():outputs=torch.cat([network(x) for x in mx.split(32)])
    cost=float(loss(outputs,mt,torch.tensor(vw[mi])));assert abs(cost-expected['monitor_loss'])<1e-4
    metrics={}
    for n,row in enumerate(outputs[:,:,1::2].transpose(1,2).numpy()):
        source=int(vi[mi[n]]);positive=bool(monitor['label'][source]);group=str(monitor['wake_group'][source]);events=decode(row[64:],offset=32768)
        lo=int(monitor['event_start_accept'][source]);hi=int(monitor['event_end'][source])+6400
        valid=lambda event:positive and lo<=event[0]<=hi and event[1]==group
        g=metrics.setdefault(group,{'count':0,'hits':0,'false':0});g['count']+=1;g['hits']+=any(valid(e) for e in events);g['false']+=any(not valid(e) for e in events)
    report={'complete':True,'best_step':best['step'],'coverage':coverage,'elapsed_training_seconds':history[-1]['elapsed_seconds'],
            'best_sha256':sha(a.run/'best.pt'),'monitor_corrected':metrics,'monitor_loss_recomputed':cost,
            'reporting_recovery':'Original loop completed4000steps and saved checkpoints; finalizer independently verifies coverage after invalid unseen_rows summary key. Monitor now excludes warm-up. No training repeated.',
            'finalizer_sha256':sha(__file__),'original_source_sha256':cfg['source_sha256']}
    (a.run/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8');print(json.dumps(report),flush=True)

if __name__=='__main__':main()
