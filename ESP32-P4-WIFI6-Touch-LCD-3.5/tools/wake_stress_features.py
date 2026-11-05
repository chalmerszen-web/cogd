"""Append audibly screened TRAIN sources with low-level/channel augmentation."""
import argparse,hashlib,json,sys
from pathlib import Path
import numpy as np
from scipy.signal import fftconvolve
from wake_prepare import ROOT,pcm,bounds
sys.path.insert(0,str(ROOT/'training/kws'))
from data import Frontend,normalize

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--previous',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True)
    p.add_argument('--capture',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--round',type=int,choices=(1,2,3),required=True)
    p.add_argument('--maximum-source-seconds',type=float,default=2.048)
    p.add_argument('--minimum-rms',type=float,default=None)
    p.add_argument('--variants',type=int,default=24)
    p.add_argument('--tag',default=None)
    a=p.parse_args();a.previous=a.previous.resolve();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    rows=[json.loads(s) for s in a.manifest.read_text(encoding='utf8').splitlines()];assert rows and all(r['split']=='train' for r in rows)
    assert not any('longanfengyue' in r['source_group'] or 'xunanchuan' in r['source_group'] for r in rows)
    norm=json.loads((a.previous/'normalization.json').read_text());channel=json.loads((a.previous/'channel.json').read_text())
    capture=json.loads(a.capture.read_text(encoding='utf8'));assert capture['complete']
    ambient=[pcm(t['path'])[4000:40000].astype(float) for t in capture['trials']]
    rng=np.random.default_rng(2026110200+a.round);front=Frontend();length=384*256;freq=np.fft.rfftfreq(512,1/16000);extra=[]
    for row in rows:
        signal=pcm(row['path']).astype(float);lo,hi=bounds(signal,maximum_samples=int(a.maximum_source_seconds*16000));signal=signal[lo:hi]
        count=a.variants if a.round<3 or row['label'] else a.variants*2
        for v in range(count):
            x=signal.copy();offset=40960 if v==0 else int(rng.integers(33792,45057))
            if v:
                curve=np.asarray(channel['median_response_db'])*(0 if v%4==0 else rng.uniform(.5,1.2))
                curve+=rng.uniform(-3,3,7)
                if a.round>=2:curve+=np.linspace(0,-rng.uniform(0,10),7)
                impulse=np.roll(np.fft.irfft(10**(np.interp(freq,channel['centers_hz'],curve)/20),n=512),256)*np.hanning(512)
                x=fftconvolve(x,impulse,mode='same')
                if a.round>=2 and v%2:
                    reverb=np.zeros(4801);reverb[0]=1
                    for delay,gain in [(int(rng.integers(240,480)),.22),(int(rng.integers(700,1100)),-.15),(int(rng.integers(1600,3200)),.08)]:reverb[delay]=gain*rng.uniform(.6,1.6)
                    x=fftconvolve(x,reverb)[:len(signal)+3200]
            minimum_rms=a.minimum_rms if a.minimum_rms is not None else (450 if a.round==1 else 280)
            assert 50<=minimum_rms<=1000 and 2<=a.variants<=48
            speech_rms=4500 if not v else float(np.exp(rng.uniform(np.log(minimum_rms),np.log(6000))))
            x*=min(speech_rms/max(np.sqrt(np.mean(x*x)),1),24000/max(np.max(abs(x)),1))
            floor=np.resize(np.roll(ambient[int(rng.integers(len(ambient)))],int(rng.integers(30000))),length)
            offset=min(offset,length-len(x)-4096)
            assert offset>=32768,'Complete source does not fit after the causal warm-up'
            audio=floor*(1 if not v else rng.uniform(.5,1.8));assert offset+len(x)+4096<=length
            audio[offset:offset+len(x)]+=x;audio=np.clip(np.rint(audio),-32768,32767).astype(np.int16)
            finish=offset+len(signal) if row['label'] else -1;labels=np.zeros(384,np.float32);ends=(np.arange(384)+1)*256
            if finish>=0:labels[(ends>=finish)&(ends<=finish+2560)]=1
            extra.append((dict(row,clip_id=(a.tag or f'stress{a.round}')+'-'+row['clip_id'],p4_domain=False,new_source=True),normalize(front(audio),norm),labels,finish,v))
        print(json.dumps({'source':row['clip_id'],'examples':len(extra)}),flush=True)
    n=len(extra);added=dict(x=np.stack([e[1] for e in extra]),y=np.stack([e[2] for e in extra]),event_end=np.array([e[3] for e in extra]),
        event_start_accept=np.array([e[3]-640 if e[3]>=0 else -1 for e in extra]),variant=np.array([e[4] for e in extra]),
        device_domain=np.ones(n,bool),legacy_device_domain=np.zeros(n,bool),sampling_group=np.array(['positive' if e[0]['label'] else 'hard_negative' for e in extra]))
    for k in ('label','language','clip_id','source_group','text','p4_domain','new_source','wake_group'):added[k]=np.array([e[0][k] for e in extra])
    counts={}
    for split in ('train','validation'):
        old=dict(np.load(a.previous/(split+'.npz'),allow_pickle=False));assert set(old)==set(added)
        mask=np.ones(n,bool) if split=='train' else added['variant']==0
        data={k:np.concatenate([old[k],added[k][mask]]) for k in old}
        assert not any(s in str(v) for v in data['source_group'] for s in ('longanfengyue','xunanchuan','af_nicole','am_adam'))
        np.savez_compressed(a.out/(split+'.npz'),**data);counts[split]=len(data['x'])
    for name in ('normalization.json','channel.json'):(a.out/name).write_bytes((a.previous/name).read_bytes())
    meta=json.loads((a.previous/'features.json').read_text());meta.update(stress_round=a.round,stress_added_examples=n,stress_minimum_rms=minimum_rms,stress_variants=a.variants,
        stress_script_sha256=sha(Path(__file__)),stress_complete_source_preserved=True,
        stress_manifest=str(a.manifest),stress_manifest_sha256=sha(a.manifest),stress_sources=rows,
        stress_previous_hashes={name:sha(a.previous/name) for name in ('train.npz','validation.npz','features.json')},
        train_examples=counts['train'],monitor_examples=counts['validation'],new_frozen_test_read=False,evaluation_scope='training_monitor')
    (a.out/'features.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps({'complete':True,'counts':counts}))
if __name__=='__main__':main()
