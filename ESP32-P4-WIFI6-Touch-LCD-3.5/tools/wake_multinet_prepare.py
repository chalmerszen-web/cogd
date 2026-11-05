"""Freeze already recorded TRAIN/DEVELOPMENT inputs for a P4 inference probe."""
import argparse,hashlib,json
from pathlib import Path
import numpy as np
from scipy.signal import butter,sosfilt,correlate,correlation_lags
from wake_prepare import ROOT,pcm,bounds

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--capture',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--curve',type=Path);a=p.parse_args()
    capture=json.loads(a.capture.read_text(encoding='utf8'));assert capture['complete'];frozen={}
    if a.curve:frozen={r['id']:r for r in json.loads(a.curve.read_text())['trials']}
    sos=butter(3,[200,6000],btype='bandpass',fs=16000,output='sos');rows=[]
    for t in capture['trials']:
        source=t['source'];assert source['split'] in ('train','validation')
        raw=pcm(t['path']);reference=pcm(source['path']);assert sha(ROOT/source['path'])==source['wav_sha256']
        if source['clip_id'] in frozen:
            meta=frozen[source['clip_id']];assert sha(ROOT/t['path'])==meta['capture_sha256'];interval=meta['accept_bounds'];quality=meta['correlation']
        else:
            assert source['split']=='train','DEVELOPMENT must use existing frozen acceptance bounds'
            r=sosfilt(sos,raw.astype(float));s=sosfilt(sos,reference.astype(float));cross=correlate(r,s,mode='full',method='fft');lags=correlation_lags(len(r),len(s))
            allowed=(lags>=50000)&(lags<=64000)&(lags+len(s)<=len(r));indices=np.flatnonzero(allowed);lag=int(lags[indices[np.argmax(abs(cross[allowed]))]])
            quality=float(abs(np.dot(r[lag:lag+len(s)],s))/np.sqrt(np.dot(r[lag:lag+len(s)],r[lag:lag+len(s)])*np.dot(s,s)));lo,hi=bounds(reference,maximum_samples=64000);interval=[lag+lo-1024,lag+hi+6400]
        rows.append({'id':source['clip_id'],'split':source['split'],'group':source['wake_group'],'label':source['label'],'path':t['path'],'sha256':sha(ROOT/t['path']),
                     'accept_bounds':interval,'correlation':quality,'samples':len(raw)})
    out={'scope':'Recorded PCM inference probe, not physical or user acceptance','capture_sha256':sha(a.capture),'trials':rows}
    assert not a.out.exists();a.out.write_text(json.dumps(out,indent=2),encoding='utf8');print(json.dumps({'trials':len(rows),'output':str(a.out)}))

if __name__=='__main__':main()
