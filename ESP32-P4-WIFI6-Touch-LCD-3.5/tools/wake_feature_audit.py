"""Inspect quantization clipping and acoustic levels in DEVELOPMENT only."""
import argparse,json,sys
from pathlib import Path
import numpy as np
from wake_prepare import ROOT,pcm
sys.path.insert(0,str(ROOT/'training/kws'))
from data import Frontend,normalize

def main():
    p=argparse.ArgumentParser();p.add_argument('--capture',type=Path,required=True);p.add_argument('--curve',type=Path,required=True)
    p.add_argument('--normalization',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    capture=json.loads(a.capture.read_text(encoding='utf8'));curve=json.loads((a.curve/'report.json').read_text());norm=json.loads(a.normalization.read_text());front=Frontend();rows=[]
    assert all(t['source']['split']=='validation' for t in capture['trials'])
    for trial,meta in zip(capture['trials'],curve['trials']):
        if not meta['label']:continue
        samples=pcm(trial['path']);raw=front(samples);features=normalize(raw,norm);low,high=meta['accept_bounds'];low=max(0,low);high=min(len(samples),high-6400)
        f=features[low//256:high//256];voice=samples[low:high].astype(np.float64);noise=samples[:32000].astype(np.float64)
        rms=lambda v:float(np.sqrt(np.mean(v*v)))
        rows.append({'id':meta['id'],'voice_rms':rms(voice),'background_rms':rms(noise),'voice_to_background_db':float(20*np.log10(rms(voice)/max(1e-9,rms(noise)))),
                     'clipped_low':float(np.mean(f==-128)),'clipped_high':float(np.mean(f==127)),
                     'band_clipped_low':np.mean(f==-128,axis=0).tolist(),'raw_logmel_range':[int(raw.min()),int(raw.max())]})
    result={'scope':'DEVELOPMENT input diagnosis, no relabeling or acceptance','trials':rows}
    a.out.write_text(json.dumps(result,indent=2),encoding='utf8')
    for group in ('nihao_zh','hi_zh','nihao_yue','hi_yue'):
        selected=[r for r,m in zip(rows,[m for m in curve['trials'] if m['label']]) if m['group']==group]
        print(group,{k:[round(float(np.min([r[k] for r in selected])),3),round(float(np.max([r[k] for r in selected])),3)] for k in ('voice_rms','background_rms','voice_to_background_db','clipped_low','clipped_high')})

if __name__=='__main__':main()
