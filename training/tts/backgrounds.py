"""Finite, reproducible synthetic noise/music negatives; no recorded voices."""
import argparse
import hashlib
import json
from pathlib import Path
import wave
import numpy as np

ROOT=Path(__file__).resolve().parents[2]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--seed-base',type=int,default=20261900);args=ap.parse_args()
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    rate=16000;n=rate*10;t=np.arange(n)/rate;rows=[]
    for index,split in enumerate(['train']*24+['validation']*8+['test']*16):
        seed=args.seed_base+index;rng=np.random.default_rng(seed);kind=index%4
        white=rng.normal(0,1,n)
        if kind==0:signal=white*.15;name='white-noise'
        elif kind==1:
            signal=np.convolve(white,np.ones(48)/48,mode='same');name='filtered-noise'
        elif kind==2:
            f=rng.choice([50,60]);signal=.3*np.sin(2*np.pi*f*t)+.08*np.sin(2*np.pi*3*f*t)+white*.015;name='hum'
        else:
            signal=white*.003;name='procedural-music'
            # New independently seeded composition for every source, not a
            # transformed copy of a composition assigned to another split.
            step=60/rng.uniform(90,150)/2
            for note in range(int(10/step)):
                start=int(note*step*rate);length=min(int(step*.9*rate),n-start)
                tt=np.arange(length)/rate;midi=int(rng.choice([48,50,52,55,57,60,62,64,67,69]))
                f=440*2**((midi-69)/12);env=np.minimum(tt/.015,1)*np.exp(-tt/ .15)
                signal[start:start+length]+=(np.sin(2*np.pi*f*tt)+.2*np.sin(4*np.pi*f*tt))*env*.22
                if note%2==0:
                    drum=np.exp(-tt/.035)*rng.normal(0,.12,length)
                    signal[start:start+length]+=drum
        pcm=np.clip(np.rint(signal*rng.uniform(.08,.7)*32768),-32768,32767).astype('<i2')
        prefix='procedural' if args.seed_base==20261900 else f'procedural-s{args.seed_base}'
        ident=f'{prefix}-{index:03d}-{name}';path=out/(ident+'.wav')
        with wave.open(str(path),'wb') as wav:
            wav.setparams((1,2,rate,0,'NONE','not compressed'));wav.writeframes(pcm.tobytes())
        rows.append(dict(clip_id=ident,path=path.relative_to(ROOT).as_posix(),label=0,language='none',
            source_group=ident,original_recording_id=ident,pcm_sha256=hashlib.sha256(pcm.tobytes()).hexdigest(),
            license_id='project-generated',split=split,seed=seed,seconds=10,source_kind=name,
            generator_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()))
    (out/'manifest.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in rows),encoding='utf8')
    print(json.dumps(dict(clips=len(rows),seconds=len(rows)*10,limitation='Synthetic negatives; not real television/music coverage')))

if __name__=='__main__':main()
