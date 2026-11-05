"""Append screened four-phrase sources and TRAIN-only P4 channel augmentation.

No final-test features are prepared by this command. Source identities retain
their split; normalization is the frozen legacy TRAIN normalization.
"""
import argparse, hashlib, json, re, sys, wave
from pathlib import Path
import numpy as np
from scipy.signal import welch, resample_poly
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'training/kws')]
from data import Frontend, framed_example, normalize
from paired_negative import incomplete

def pcm(path):
    with wave.open(str(ROOT/path),'rb') as w:
        assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(1,2,16000)
        return np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').copy()

def clean(text): return re.sub(r'[^a-z\u4e00-\u9fff]','',text.lower())

def screened(row,text):
    value=clean(text).replace('賢','贤').replace('楊','杨')
    if not row['label']:
        # Keep only unambiguous new near-misses; never teach a target as negative.
        expected=clean(row['text'])
        aliases={'hi':{'hi','嗨','hai'},'小言':{'小言','小严','小妍','小颜','小岩','小贤','小然','小燃','小延','小弦','小研'}}
        if expected in aliases:return value in aliases[expected]
        name=expected[-2:]
        return value in {'hi'+name,'嗨'+name,'hai'+name}
    name={'小言','小严','小妍','小颜','小岩','小炎','小研'} if row['language']=='zh' else {'小言','小贤','小妍','小然','小燃','小延','小弦','小研'}
    prefix={'你好'} if row['wake_group'].startswith('nihao') else {'hi','嗨','hai'}
    return value in {p+n for p in prefix for n in name}

def bounds(signal,device=False,maximum_samples=32768):
    energy=np.sqrt(np.mean(np.pad(signal.astype(float),(0,(-len(signal))%160)).reshape(-1,160)**2,axis=1))
    floor=max(80, float(np.median(energy[:20]))*3) if device else 64
    active=np.flatnonzero(energy>=max(floor,float(energy.max())*.035))
    if not len(active):raise ValueError('no_speech')
    start=max(0,(int(active[0])-2)*160);end=min(len(signal),(int(active[-1])+4)*160)
    if not 3200<=end-start<=maximum_samples:raise ValueError(f'duration_outside_3200_to_{maximum_samples}_samples')
    return start,end

def main():
    p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--captures',type=Path,nargs='+',required=True)
    p.add_argument('--manifest',type=Path);p.add_argument('--audit',type=Path)
    p.add_argument('--augment-manifest',type=Path);p.add_argument('--tempo',action='store_true')
    p.add_argument('--native-development-to-train',action='store_true',
                   help='Explicitly retire all Rocky development sources into TRAIN; Kiki TEST stays untouched')
    a=p.parse_args();a.base=a.base.resolve();a.out=a.out.resolve()
    a.out.mkdir(parents=True,exist_ok=False)
    manifest=(a.manifest or a.base/'synthetic/manifest.jsonl').resolve();audit_path=(a.audit or a.base/'synthetic/asr.json').resolve()
    synthetic=[json.loads(s) for s in manifest.read_text(encoding='utf8').splitlines()]
    audits={r['clip_id']:r for r in json.loads(audit_path.read_text(encoding='utf8'))['clips']}
    captures=[]
    for path in a.captures:
        report=json.loads(path.read_text(encoding='utf8'));assert report['complete'],path
        captures.extend(report['trials'])
    if a.native_development_to_train:
        for row in synthetic+[t['source'] for t in captures]:
            if row['source_group']=='qwen3-system-Rocky':
                assert row['split']=='validation' or (row['split']=='train' and row.get('previous_split')=='validation')
                row['previous_split']='validation';row['split']='train'
    legacy=ROOT/'artifacts/kws-bilingual/features-compact'
    stats=json.loads((legacy/'normalization.json').read_text())
    old={s:dict(np.load(legacy/(s+'.npz'),allow_pickle=False)) for s in ('train','validation')}
    groups={s:set(d['source_group']) for s,d in old.items()}
    # These native presets were frozen before any KWS scoring. Rocky is only
    # development validation; Kiki remains test and is never loaded below.
    groups['train' if a.native_development_to_train else 'validation'].add('qwen3-system-Rocky')
    assert not groups['train']&groups['validation']
    noises=[];responses=[];frequencies=np.fft.rfftfreq(512,1/16000)
    for t in captures:
        if t['source']['split']!='train':continue
        raw=pcm(t['path']);noises.append(raw[-8000:])
        if not t['source']['label']:continue
        source=pcm(t['source']['path']);lo,hi=bounds(raw,True);sl,sh=bounds(source)
        _,sp=welch(source[sl:sh].astype(float),16000,nperseg=512)
        _,rp=welch(raw[lo:hi].astype(float),16000,nperseg=512)
        ratio=10*np.log10((rp+1e-4)/(sp+1e-4));ratio-=np.median(ratio[(frequencies>250)&(frequencies<6000)])
        responses.append(ratio)
    assert len(noises)>=4 and len(responses)>=4
    centers=np.array([125,250,500,1000,2000,4000,7500])
    response=np.median(np.stack(responses),axis=0)
    response=np.array([np.median(response[(frequencies>=f*.7)&(frequencies<=f*1.4)]) for f in centers])
    response=np.clip(response,-24,24)
    channel={'training_captures':len(responses),'centers_hz':centers.tolist(),'median_response_db':response.tolist(),
             'rule':'TRAIN source/recording Welch ratio, overall gain removed, bounded 24 dB; no validation recordings'}
    (a.out/'channel.json').write_text(json.dumps(channel,indent=2),encoding='utf8')
    selected=[];rejected=[]
    for row in synthetic:
        if row['split']=='test':continue
        assert row['source_group'] in groups[row['split']]
        audit=audits[row['clip_id']]
        if not screened(row,audit['asr_text']):
            rejected.append({'clip_id':row['clip_id'],'reason':'ASR ambiguous or incomplete','transcript':audit['asr_text']});continue
        signal=pcm(row['path'])
        try:lo,hi=bounds(signal)
        except ValueError as e:rejected.append({'clip_id':row['clip_id'],'reason':str(e)});continue
        selected.append((dict(row,wake_start_sample=lo,wake_end_sample=hi,p4_domain=False,new_source=True),signal))
    admitted={r[0]['clip_id'] for r in selected}
    for t in captures:
        parent=t['source'];assert parent['source_group'] in groups[parent['split']]
        if parent['clip_id'].startswith('p4-wake4-') and parent['clip_id'] not in admitted:continue
        signal=pcm(t['path'])
        try:lo,hi=bounds(signal,True)
        except ValueError as e:rejected.append({'clip_id':'captured-'+parent['clip_id'],'reason':str(e)});continue
        # Retain an actual room floor for augmentation, but frame the complete utterance.
        cut=signal[lo:hi]
        row=dict(parent,clip_id='captured-'+parent['clip_id'],path=t['path'],parent_clip_id=parent['clip_id'],
                 wake_start_sample=0,wake_end_sample=len(cut),p4_domain=True,new_source=True,
                 pcm_sha256=hashlib.sha256(signal.tobytes()).hexdigest())
        selected.append((row,cut))
    if a.augment_manifest:
        for parent in map(json.loads,a.augment_manifest.read_text(encoding='utf8').splitlines()):
            assert parent['split']=='train' and parent['constructed']
            assert set(parent['parent_source_groups'])<=groups['train']
            signal=pcm(parent['path']);assert hashlib.sha256(signal.tobytes()).hexdigest()==parent['pcm_sha256']
            selected.append((dict(parent,p4_domain=False,new_source=True),signal))
    frontend=Frontend();rng=np.random.default_rng(2026110101);added={s:[] for s in old}
    for index,(row,signal) in enumerate(selected):
        for stale in ('endpoint_schema','speech_start_sample','speech_end_low_sample','speech_end_high_sample'):
            row.pop(stale,None)
        variants=[(row,signal)]
        if row['label'] and row['split']=='train':
            for side in ('prefix','suffix'):
                part,_=incomplete(signal,row['wake_start_sample'],row['wake_end_sample'],side)
                variants.append((dict(row,label=0,clip_id=row['clip_id']+'-'+side,wake_group='negative',text='time-cut-'+side),part))
        for r,raw in variants:
            for variant in range(7 if r['split']=='train' else 1):
                noise=noises[int(rng.integers(len(noises)))] if variant else None
                # Short negative words need the same post-warm-up placement as
                # positives. The generic long-negative path starts at zero and
                # would hide Hi/name-only examples inside the 2 s warm-up.
                placement=dict(r,label=1)
                source=raw
                if a.tempo and variant:
                    speed=int(rng.choice([85,95,105,115]))
                    if (r['wake_end_sample']-r['wake_start_sample'])*100/speed>32768:speed=115
                    source=resample_poly(raw.astype(float),100,speed)
                    source=np.clip(np.rint(source),-32768,32767).astype(np.int16)
                    placement['wake_start_sample']=int(round(r['wake_start_sample']*100/speed))
                    placement['wake_end_sample']=min(len(source),int(round(r['wake_end_sample']*100/speed)))
                wave16,labels,end=framed_example(placement,source,rng if variant else None,background=noise)
                assert end>=33792
                if not r['label']:labels.fill(0);end=None
                if variant and not r['p4_domain']:
                    curve=(response if variant%2 else np.zeros_like(response))+rng.uniform(-5,5,len(response))
                    amp=10**(np.interp(frequencies,centers,curve)/20)
                    impulse=np.roll(np.fft.irfft(amp,n=512),256)*np.hanning(512)
                    colored=np.convolve(wave16.astype(float),impulse,mode='same')
                    wave16=np.clip(np.rint(colored),-32768,32767).astype(np.int16)
                added[r['split']].append((r,normalize(frontend(wave16),stats),labels,end,variant))
        if (index+1)%20==0:print(json.dumps({'prepared_sources':index+1,'total':len(selected)}),flush=True)
    for split,d in old.items():
        rows=added[split];count=len(d['x'])
        # Keep old device provenance separate: device_domain below means this P4.
        d['legacy_device_domain']=d['device_domain'].copy();d['device_domain']=np.zeros(count,dtype=bool)
        d['p4_domain']=np.zeros(count,dtype=bool);d['new_source']=np.zeros(count,dtype=bool)
        d['wake_group']=np.array(['nihao_'+str(l) if label else 'negative' for l,label in zip(d['language'],d['label'])])
        extra={'x':np.stack([e[1] for e in rows]),'y':np.stack([e[2] for e in rows]),
            'event_end':np.array([e[3] if e[3] is not None else -1 for e in rows]),
            'event_start_accept':np.array([e[3]-640 if e[3] is not None else -1 for e in rows]),
            'variant':np.array([e[4] for e in rows]),'device_domain':np.array([e[0]['p4_domain'] for e in rows]),
            'legacy_device_domain':np.zeros(len(rows),dtype=bool),
            'sampling_group':np.array(['positive' if e[0]['label'] else 'paired_negative' if e[0]['text'].startswith('time-cut') else 'hard_negative' for e in rows])}
        for key in ('label','language','clip_id','source_group','text','p4_domain','new_source','wake_group'):
            extra[key]=np.array([e[0].get(key,'nihao_'+e[0]['language'] if e[0]['label'] else 'negative') for e in rows])
        np.savez_compressed(a.out/(split+'.npz'),**{key:np.concatenate((d[key],extra[key])) for key in d})
    (a.out/'normalization.json').write_text(json.dumps(stats,indent=2)+'\n',encoding='utf8')
    inputs=[manifest,audit_path,*[x.resolve() for x in a.captures],Path(__file__).resolve()]
    if a.augment_manifest:inputs.append(a.augment_manifest.resolve())
    report={'legacy_features':legacy.relative_to(ROOT).as_posix(),'new_sources':[r for r,_ in selected],
            'retired_development_source_groups':['qwen3-system-Rocky'] if a.native_development_to_train else [],
            'tempo_pitch_coupled_training_only':[.85,.95,1.05,1.15] if a.tempo else False,
            'rejected':rejected,'no_test_read':True,'added_examples':{s:len(v) for s,v in added.items()},
            'source_hashes':{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}
    (a.out/'features.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({'complete':True,'added':report['added_examples'],'rejected':len(rejected)}),flush=True)

if __name__=='__main__': main()
