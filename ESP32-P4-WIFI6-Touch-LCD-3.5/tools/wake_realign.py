"""Preserve full P4 utterances and score every utterance after warm-up.

Replace energy-cropped microphone examples with reference-correlated captures.
The original development identities have already been retired into TRAIN.
Only that TRAIN monitor is retained. Frozen final-test audio is never read.
Training sequences grow to 384 frames; the embedded topology stays unchanged.
"""
import argparse,hashlib,json,sys
from pathlib import Path
import numpy as np
from scipy.signal import butter,sosfilt,correlate,correlation_lags
from wake_prepare import ROOT,pcm,bounds
sys.path[:0]=[str(ROOT/'training/kws')]
from data import Frontend,normalize
from paired_negative import incomplete

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def align(source,recording):
    sos=butter(3,[200,6000],btype='bandpass',fs=16000,output='sos')
    s=sosfilt(sos,source.astype(float));r=sosfilt(sos,recording.astype(float))
    cc=correlate(r,s,mode='full',method='fft');lags=correlation_lags(len(r),len(s),mode='full')
    valid=(lags>=3200)&(lags<=16000)&(lags+len(s)<=len(r)+1600)
    assert valid.any()
    at=np.flatnonzero(valid)[np.argmax(np.abs(cc[valid]))];lag=int(lags[at])
    end=min(len(r),lag+len(s));n=end-lag
    quality=float(abs(np.dot(r[lag:end],s[:n]))/np.sqrt(np.dot(r[lag:end],r[lag:end])*np.dot(s[:n],s[:n])))
    assert quality>=.25,(lag,quality)
    return lag,quality

def main():
    p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--extra-captures',type=Path,nargs='*',default=[]);a=p.parse_args()
    b=a.base.resolve();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);previous=b/'features-v6b'
    metadata=json.loads((previous/'features.json').read_text());assert metadata['development_reused_for_training']
    norm=json.loads((previous/'normalization.json').read_text());front=Frontend()
    silence=normalize(front(np.zeros(32768,dtype=np.int16)),norm)
    data={}
    for split in ('train','validation'):
        d=dict(np.load(previous/(split+'.npz'),allow_pickle=False));keep=~d['p4_domain']
        d={k:v[keep] for k,v in d.items()}
        assert not any(v in str(s) for s in d['source_group'] for v in ('af_bella','am_fenrir','Kiki'))
        d['x']=np.concatenate([np.broadcast_to(silence,(len(d['x']),128,40)),d['x']],axis=1)
        d['y']=np.pad(d['y'],((0,0),(128,0)))
        for k in ('event_end','event_start_accept'):d[k]=np.where(d[k]>=0,d[k]+32768,d[k])
        data[split]=d
    groups=set(data['train']['source_group']);rng=np.random.default_rng(2026110107);extra=[];ancestry=[];inputs=[]
    paths=[b/d/'capture-report.json' for d in ('capture-diagnosis-03','capture-zh','capture-yue-train','capture-yue-val','capture-hardening','capture-development-retirement')]
    paths.extend(p.resolve() for p in a.extra_captures)
    assert len(paths)==len(set(paths))
    counts={}
    for path in paths:
        directory=path.parent.name
        report=json.loads(path.read_text(encoding='utf8'));assert report['complete'];inputs.append(path)
        for t in report['trials']:
            parent=t['source'];assert parent['source_group'] in groups and parent['split']!='test'
            source=pcm(parent['path']);recording=pcm(t['path']);lag,quality=align(source,recording)
            sl,sh=bounds(source);start=max(0,lag-800);end=min(len(recording),lag+len(source)+1600)
            assert lag+sh<=len(recording),parent['clip_id']
            raw=recording[start:end];wordstart=lag+sl-start;wordend=lag+sh-start
            # This prefix is proven to precede the played source. A trailing
            # half-second is unsafe: it can contain the word's quiet final syllable.
            noise=recording[:min(4000,lag-800)].astype(float);assert len(noise)>=2400
            row=dict(parent,clip_id='aligned-'+directory+'-'+parent['clip_id'],split='train',p4_domain=True,new_source=True,
                     wake_group=parent.get('wake_group','nihao_'+parent['language'] if parent['label'] else 'negative'))
            variants=[(row,raw,wordend)]
            if row['label']:
                for side in ('prefix','suffix'):
                    part,_=incomplete(raw,wordstart,wordend,side)
                    variants.append((dict(row,label=0,clip_id=row['clip_id']+'-'+side,wake_group='negative',text='time-cut-'+side),part,None))
            for r,signal,event_end in variants:
                for variant in range(7):
                    # Entire audio starts AFTER the 2.048-second decision warm-up.
                    offset=int(rng.integers(33280,40961)) if variant else 36864
                    assert offset+len(signal)+4096<=384*256,(r['clip_id'],len(signal))
                    wave=np.zeros(384*256,dtype=float);wave[offset:offset+len(signal)]=signal
                    if variant:
                        wave*=10**(rng.uniform(-10,2)/20)
                        wave+=np.resize(noise,len(wave))*rng.uniform(.2,1.0)
                    wave=np.clip(np.rint(wave),-32768,32767).astype(np.int16)
                    label=np.zeros(384,dtype=np.float32);finish=offset+event_end if r['label'] else -1
                    ends=(np.arange(384)+1)*256
                    if finish>=0:label[(ends>=finish)&(ends<=finish+2560)]=1
                    extra.append((r,normalize(front(wave),norm),label,finish,variant))
            ancestry.append({'id':row['clip_id'],'correlation':quality,'lag_samples':lag,'complete_source_bounds':[sl,sh],
                             'capture_crop':[start,end],'aligned_word_bounds':[wordstart,wordend],
                             'source':parent['path'],'capture':t['path'],'capture_sha256':sha(ROOT/t['path'])})
    n=len(extra)
    added={'x':np.stack([v[1] for v in extra]),'y':np.stack([v[2] for v in extra]),
           'event_end':np.array([v[3] for v in extra]),'event_start_accept':np.array([v[3]-640 if v[3]>=0 else -1 for v in extra]),
           'variant':np.array([v[4] for v in extra]),'device_domain':np.ones(n,dtype=bool),'legacy_device_domain':np.zeros(n,dtype=bool),
           'sampling_group':np.array(['positive' if v[0]['label'] else 'paired_negative' if v[0]['text'].startswith('time-cut') else 'hard_negative' for v in extra])}
    for k in ('label','language','clip_id','source_group','text','p4_domain','new_source','wake_group'):added[k]=np.array([v[0][k] for v in extra])
    for split,d in data.items():
        chosen=np.ones(n,dtype=bool) if split=='train' else added['variant']==0
        assert set(d)==set(added)
        counts[split]=len(d['x'])+int(chosen.sum())
        np.savez_compressed(out/(split+'.npz'),**{k:np.concatenate([d[k],added[k][chosen]]) for k in d})
    for name in ('normalization.json','channel.json'):(out/name).write_bytes((previous/name).read_bytes())
    metadata.update(sequence_frames=384,embedded_topology_changed=False,all_speech_post_warmup=True,
        capture_alignment=ancestry,removed_previous_p4_examples=True,new_capture_examples=n,
        train_examples=counts['train'],monitor_examples=counts['validation'],
        source_hashes={p.relative_to(ROOT).as_posix():sha(p) for p in [*inputs,previous/'train.npz',previous/'validation.npz',Path(__file__).resolve()]})
    (out/'features.json').write_text(json.dumps(metadata,indent=2),encoding='utf8')
    print(json.dumps({'complete':True,'captures':len(ancestry),'added':n,'frames':384,'no_test_read':True}))

if __name__=='__main__':main()
