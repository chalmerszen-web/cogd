"""Freeze reproducible acoustic conditions; these are not measured directions."""
import argparse,hashlib,json,sys,wave
from pathlib import Path
import numpy as np
from scipy.signal import fftconvolve,butter,sosfilt
ROOT=Path(__file__).resolve().parents[2]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--compact',action='store_true');a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    rows=[json.loads(s) for s in a.manifest.read_text(encoding='utf8').splitlines()]
    if a.compact:
        rows=[r for g in ('nihao_zh','hi_zh','nihao_yue','hi_yue','negative') for r in [x for x in rows if x.get('wake_group')==g][:2 if g!='negative' else 4]]
    records=[]
    for r in rows:
        path=ROOT/r['path'];assert sha(path)==r['wav_sha256']
        with wave.open(str(path),'rb') as w:
            assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(1,2,16000)
            raw=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(float)
        rms=np.sqrt(np.mean(raw**2));level=min(.14*32768/max(rms,1),.8*32768/max(abs(raw).max(),1));base=raw*level
        for condition,gain in [('quiet',.10),('very_quiet',.06),('filtered_reflections',.10)]:
            x=base.copy()
            if condition=='filtered_reflections':
                # 2 kHz shelf and deterministic early reflections. The speaker
                # and physical board remain in their original positions.
                low=sosfilt(butter(2,2000,fs=16000,output='sos'),x)
                x=.4*x+.6*low
                impulse=np.zeros(1601);impulse[0]=1;impulse[368]=.22;impulse[848]=-.13;impulse[1520]=.07
                x=fftconvolve(x,impulse)
            x=np.clip(np.rint(x),-32768,32767).astype('<i2');ident=r['clip_id']+'-'+condition;target=a.out/(ident+'.wav')
            with wave.open(str(target),'wb') as w:w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(x.tobytes())
            records.append(dict(r,clip_id=ident,parent_clip_id=r['clip_id'],parent_wav_sha256=r['wav_sha256'],
                path=target.relative_to(ROOT).as_posix(),wav_sha256=sha(target),pcm_sha256=hashlib.sha256(x.tobytes()).hexdigest(),
                playback_gain=gain,prepared_level=True,condition=condition,seconds=len(x)/16000,
                condition_scope='Software attenuation/filter/reflections, no physical angle or calibrated distance'))
    out=a.out/'manifest.jsonl';out.write_text(''.join(json.dumps(r,ensure_ascii=False)+'\n' for r in records),encoding='utf8')
    (a.out/'plan.json').write_text(json.dumps({'sources':len(rows),'trials':len(records),'manifest_sha256':sha(out),
        'prepared_level':True,'conditions':['quiet','very_quiet','filtered_reflections'],'physical_positions_changed':False},indent=2),encoding='utf8')
    print(json.dumps({'sources':len(rows),'trials':len(records),'manifest_sha256':sha(out)}))
if __name__=='__main__':main()
