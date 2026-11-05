"""Final fit corpus: explicitly retire all development identities into TRAIN.

The retained evaluation NPZ is now a TRAIN monitor, never independent evidence.
The frozen TEST identities/audio remain excluded and unchanged.
"""
import argparse,hashlib,json,shutil,sys
from pathlib import Path
import numpy as np
from wake_prepare import ROOT,pcm,bounds
sys.path[:0]=[str(ROOT/'training/kws')]
from data import Frontend,framed_example,normalize
from paired_negative import incomplete

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    b=a.base.resolve();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);previous=b/'features-v5'
    train=dict(np.load(previous/'train.npz',allow_pickle=False));monitor=dict(np.load(previous/'validation.npz',allow_pickle=False))
    retired=sorted(str(x) for x in set(monitor['source_group'])-set(train['source_group']) if str(x).startswith('kokoro-v1-'))
    assert retired==['kokoro-v1-zf_xiaoxiao','kokoro-v1-zm_yunxia']
    for d in (train,monitor):assert not any(v in str(s) for s in d['source_group'] for v in ('af_bella','am_fenrir','Kiki'))
    capture= b/'capture-development-retirement/capture-report.json';report=json.loads(capture.read_text(encoding='utf8'));assert report['complete']
    statistics=json.loads((previous/'normalization.json').read_text());frontend=Frontend();rng=np.random.default_rng(2026110105)
    extra=[];ancestry=[];noises=[]
    for t in report['trials']:noises.append(pcm(t['path'])[-8000:])
    for t in report['trials']:
        parent=t['source'];assert parent['split']=='validation' and parent['source_group'] in retired
        raw=pcm(t['path']);lo,hi=bounds(raw,True);signal=raw[lo:hi]
        r=dict(parent,clip_id='final-fit-'+parent['clip_id'],wake_start_sample=0,wake_end_sample=len(signal),split='train',
               p4_domain=True,new_source=True,wake_group=parent.get('wake_group','negative' if not parent['label'] else 'nihao_'+parent['language']))
        for k in ('endpoint_schema','speech_start_sample','speech_end_low_sample','speech_end_high_sample'):r.pop(k,None)
        variants=[(r,signal)]
        if r['label']:
            for side in ('prefix','suffix'):
                part,_=incomplete(signal,0,len(signal),side)
                variants.append((dict(r,label=0,clip_id=r['clip_id']+'-'+side,wake_group='negative',text='time-cut-'+side),part))
        for row,source in variants:
            for variant in range(7):
                wave,labels,end=framed_example(dict(row,label=1),source,rng if variant else None,
                    background=noises[int(rng.integers(len(noises)))] if variant else None)
                if not row['label']:labels.fill(0);end=None
                extra.append((row,normalize(frontend(wave),statistics),labels,end,variant))
        ancestry.append({'clip_id':r['clip_id'],'parent':parent['clip_id'],'capture_path':t['path'],'pcm_sha256':sha(ROOT/t['path']),'former_split':'validation','now':'train'})
    n=len(extra);added={
        'x':np.stack([e[1] for e in extra]),'y':np.stack([e[2] for e in extra]),
        'event_end':np.array([e[3] if e[3] is not None else -1 for e in extra]),
        'event_start_accept':np.array([e[3]-640 if e[3] is not None else -1 for e in extra]),
        'variant':np.array([e[4] for e in extra]),'device_domain':np.ones(n,dtype=bool),
        'legacy_device_domain':np.zeros(n,dtype=bool),
        'sampling_group':np.array(['positive' if e[0]['label'] else 'paired_negative' if e[0]['text'].startswith('time-cut') else 'hard_negative' for e in extra])}
    for k in ('label','language','clip_id','source_group','text','p4_domain','new_source','wake_group'):added[k]=np.array([e[0][k] for e in extra])
    assert set(added)==set(train)==set(monitor)
    moved=np.isin(monitor['source_group'],retired)
    combined={k:np.concatenate([train[k],monitor[k][moved],added[k]]) for k in train}
    native=(train['source_group']=='qwen3-system-Rocky') & (train['variant']==0)
    once=added['variant']==0
    rehearsal={k:np.concatenate([monitor[k],train[k][native],added[k][once]]) for k in train}
    np.savez_compressed(out/'train.npz',**combined);np.savez_compressed(out/'validation.npz',**rehearsal)
    for name in ('normalization.json','channel.json'):shutil.copy2(previous/name,out/name)
    metadata={'development_reused_for_training':True,'evaluation_scope':'training_monitor_only',
        'retired_development_source_groups':retired+['qwen3-system-Rocky'],'no_test_read':True,
        'unmoved_development_negative_groups':sorted(str(x) for x in set(monitor['source_group'][~moved])),
        'frozen_test_sha256':sha(b/'heldout-physical-manifest.jsonl'),'train_examples':len(combined['x']),
        'monitor_examples':len(rehearsal['x']),'added_capture_examples':n,'capture_ancestry':ancestry,
        'source_hashes':{str(x.relative_to(ROOT)):sha(x) for x in [previous/'train.npz',previous/'validation.npz',capture,Path(__file__).resolve()]}}
    (out/'features.json').write_text(json.dumps(metadata,indent=2),encoding='utf8');print(json.dumps(metadata))

if __name__=='__main__':main()
