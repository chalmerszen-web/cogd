"""Add complete live KWS TRAIN windows, retaining their pre-word room noise.

The held-out round-three sources are forbidden. All records used here were
already retired to TRAIN before capture. Monitoring is not independent testing.
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

def main():
    p=argparse.ArgumentParser();p.add_argument('--previous',type=Path,required=True);p.add_argument('--captures',type=Path,nargs='+',required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--minimum-correlation',type=float,default=.25)
    p.add_argument('--lag-min',type=int,default=40000);p.add_argument('--lag-max',type=int,default=80000)
    p.add_argument('--skip-uncertain-alignment',action='store_true')
    p.add_argument('--maximum-source-seconds',type=float,default=2.048)
    a=p.parse_args();assert .08<=a.minimum_correlation<=1 and 40000<=a.lag_min<a.lag_max<=80000
    a.out=a.out.resolve();a.captures=[c.resolve() for c in a.captures];a.out.mkdir(parents=True,exist_ok=False);excluded=[]
    prior=a.previous.resolve();norm=json.loads((prior/'normalization.json').read_text());front=Frontend()
    rng=np.random.default_rng(2026110112);extra=[];ancestry=[];length=384*256
    sos=butter(3,[200,6000],btype='bandpass',fs=16000,output='sos')
    freq=np.fft.rfftfreq(512,1/16000);centers=[0,200,500,1000,2000,4000,8000]
    for cp in a.captures:
        captured=json.loads(cp.read_text(encoding='utf8'));assert captured['complete']
        for ti,t in enumerate(captured['trials']):
            parent=t['source'];assert parent['split']=='train' and not any(s in parent['source_group'] for s in ('af_nicole','am_adam'))
            raw=pcm(t['path']);source=pcm(parent['path']);assert sha(ROOT/parent['path'])==parent['wav_sha256']
            s=sosfilt(sos,source.astype(float));r=sosfilt(sos,raw.astype(float))
            cc=correlate(r,s,mode='full',method='fft');lags=correlation_lags(len(r),len(s),mode='full')
            valid=(lags>=a.lag_min)&(lags<=a.lag_max)&(lags+len(s)<=len(r))
            candidates=np.flatnonzero(valid);assert len(candidates)
            lag=int(lags[candidates[np.argmax(np.abs(cc[valid]))]])
            quality=float(abs(np.dot(r[lag:lag+len(s)],s))/np.sqrt(np.dot(r[lag:lag+len(s)],r[lag:lag+len(s)])*np.dot(s,s)))
            if quality<a.minimum_correlation and a.skip_uncertain_alignment:
                excluded.append(dict(id=parent['clip_id'],reason='source correlation below predeclared alignment floor',quality=quality,lag=lag));continue
            assert quality>=a.minimum_correlation,(parent['clip_id'],quality)
            lo,hi=bounds(source,maximum_samples=int(a.maximum_source_seconds*16000));start=max(0,((lag-40960)//256)*256)
            signal=raw[start:start+length].copy();assert len(signal)==length
            begin=lag+lo-start;end=lag+hi-start;assert 32768<=begin<end<=length-4096
            # This prefix precedes the source by at least half a second.
            ambient=raw[4000:40000].astype(float);assert lag>=48000
            row=dict(parent,clip_id=f'live-{cp.parent.name}-{ti:03}-{parent["clip_id"]}',p4_domain=True,new_source=True,
                     wake_group=parent.get('wake_group','nihao_'+parent['language'] if parent['label'] else 'negative'))
            variants=[(row,signal)]
            if parent['label']:
                for side in ('prefix','suffix'):
                    part,_=incomplete(signal,begin,end,side,ambient=ambient)
                    variants.append((dict(row,label=0,clip_id=row['clip_id']+'-'+side,wake_group='negative',text='time-cut-'+side),part))
            for vr,base in variants:
                for v in range(12):
                    audio=base.astype(float);offset=0
                    if v:
                        offset=int(rng.integers(-512,513));audio=np.roll(audio,offset)
                        # Change speech and noise together, then independently
                        # restore a room floor. No zero-only leading context.
                        audio*=10**(rng.uniform(-9,4)/20)
                        if v%3==0:
                            curve=rng.uniform(-2.5,2.5,len(centers));amp=10**(np.interp(freq,centers,curve)/20)
                            impulse=np.roll(np.fft.irfft(amp,n=512),256)*np.hanning(512)
                            audio=np.convolve(audio,impulse,mode='same')
                        audio+=np.resize(np.roll(ambient,int(rng.integers(len(ambient)))),length)*rng.uniform(0,.7)
                    audio=np.clip(np.rint(audio),-32768,32767).astype(np.int16)
                    finish=end+offset if vr['label'] else -1;labels=np.zeros(384,np.float32);ends=(np.arange(384)+1)*256
                    if finish>=0:labels[(ends>=finish)&(ends<=finish+2560)]=1
                    extra.append((vr,normalize(front(audio),norm),labels,finish,v))
            ancestry.append(dict(id=row['clip_id'],parent=parent['clip_id'],source_group=parent['source_group'],capture=t['path'],
                                 capture_sha256=sha(ROOT/t['path']),lag=lag,correlation=quality,crop=[start,start+length],
                                 wake_bounds=[begin,end],source_sha256=parent['wav_sha256']))
            if (ti+1)%10==0:print(json.dumps(dict(captures=ti+1,examples=len(extra))),flush=True)
    assert ancestry,'No reliably aligned TRAIN captures'
    n=len(extra);added=dict(x=np.stack([x[1] for x in extra]),y=np.stack([x[2] for x in extra]),
        event_end=np.array([x[3] for x in extra]),event_start_accept=np.array([x[3]-640 if x[3]>=0 else -1 for x in extra]),
        variant=np.array([x[4] for x in extra]),device_domain=np.ones(n,bool),legacy_device_domain=np.zeros(n,bool),
        sampling_group=np.array(['positive' if x[0]['label'] else 'paired_negative' if x[0]['text'].startswith('time-cut') else 'hard_negative' for x in extra]))
    for key in ('label','language','clip_id','source_group','text','p4_domain','new_source','wake_group'):added[key]=np.array([x[0][key] for x in extra])
    counts={}
    for split in ('train','validation'):
        data=dict(np.load(prior/(split+'.npz'),allow_pickle=False));assert set(data)==set(added)
        keep=np.ones(n,bool) if split=='train' else added['variant']==0
        combined={k:np.concatenate([data[k],added[k][keep]]) for k in data}
        assert not any(s in str(v) for v in combined['source_group'] for s in ('af_nicole','am_adam'))
        np.savez_compressed(a.out/(split+'.npz'),**combined);counts[split]=len(combined['x'])
    for name in ('normalization.json','channel.json'):(a.out/name).write_bytes((prior/name).read_bytes())
    meta=json.loads((prior/'features.json').read_text());meta.update(live_input_captures=ancestry,live_input_added_examples=n,
        live_input_capture_hashes={p.relative_to(ROOT).as_posix():sha(p) for p in a.captures},
        previous_feature_hashes={name:sha(prior/name) for name in ('train.npz','validation.npz','features.json')},
        live_input_preparation_source_sha256=sha(Path(__file__)),train_examples=counts['train'],monitor_examples=counts['validation'],
        no_round3_test_read=True,evaluation_scope='training_monitor',alignment_exclusions=excluded,
        alignment_policy={'minimum_correlation':a.minimum_correlation,'lag_min':a.lag_min,'lag_max':a.lag_max,'selection_uses_kws_scores':False})
    (a.out/'features.json').write_text(json.dumps(meta,indent=2),encoding='utf8')
    print(json.dumps(dict(complete=True,added=n,counts=counts)),flush=True)

if __name__=='__main__':main()
