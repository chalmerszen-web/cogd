"""Clean-file DEVELOPMENT diagnostic; explicitly not microphone acceptance."""
import argparse,ctypes as C,hashlib,json
from pathlib import Path
import numpy as np
from wake_host import ROOT,Oracle,build
from wake_prepare import pcm,bounds

def main():
    p=argparse.ArgumentParser();p.add_argument('--models',type=Path,required=True)
    p.add_argument('--manifest',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    rows=[json.loads(s) for s in a.manifest.read_text(encoding='utf8').splitlines() if s.strip()]
    assert rows and all(r['split']=='validation' for r in rows)
    lib=build(a.models,a.out/'host');oracle=Oracle(lib,268);dll=oracle.lib
    dll.decision_reset.argtypes=[C.c_int];dll.decision_step.argtypes=[C.c_int,C.c_int,C.c_uint64];dll.decision_step.restype=C.c_int
    result={'scope':'Clean digital DEVELOPMENT only; no loudspeaker/microphone and no physical acceptance',
            'complete':False,'rows':[],'thresholds':{},'source_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    for target_rms in (90,300,1200):
        for row in rows:
            path=ROOT/row['path'];assert hashlib.sha256(path.read_bytes()).hexdigest()==row['wav_sha256']
            x=pcm(row['path']).astype(float);lo,hi=bounds(x.astype(np.int16),maximum_samples=64000)
            gain=min(target_rms/max(1,np.sqrt(np.mean(x[lo:hi]**2))),30000/max(1,np.max(abs(x))))
            raw=np.zeros(270*512,np.int16);raw[32768:32768+len(x)]=np.rint(x*gain).astype(np.int16)
            scores=np.zeros(5,np.int16);zero=np.zeros(512,np.int16);assert dll.reset(268)==0
            for _ in range(64):dll.feed(zero.ctypes.data_as(C.POINTER(C.c_int16)),scores.ctypes.data_as(C.POINTER(C.c_int16)))
            sequence=[]
            for block in raw.reshape(-1,512):
                assert dll.feed(block.ctypes.data_as(C.POINTER(C.c_int16)),scores.ctypes.data_as(C.POINTER(C.c_int16)))>=0
                sequence.append(scores.copy())
            sequence=np.asarray(sequence);trials={}
            for threshold in (500,600,740,820):
                q=int(np.floor(np.log(threshold/(1000-threshold))*256+.5));dll.decision_reset(q)
                events=[(i+1)*512 for i,s in enumerate(sequence) if dll.decision_step(int(s[3]),int(s[4]),32768+(i+1)*512)]
                low,high=32768+lo-1024,32768+hi+6400
                hit=bool(row['label']) and any(low<=e<=high for e in events)
                false=any(not row['label'] or not low<=e<=high for e in events)
                trials[str(threshold)]={'hit':hit,'false':false,'events':events}
                group=result['thresholds'].setdefault(str(threshold),{}).setdefault(str(target_rms),{}).setdefault(row['wake_group'],{'count':0,'hits':0,'false':0})
                group['count']+=1;group['hits']+=int(hit);group['false']+=int(false)
            result['rows'].append({'id':row['clip_id'],'target_rms':target_rms,'gain':gain,'trials':trials})
    result['complete']=True;(a.out/'report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(result['thresholds'],ensure_ascii=False))

if __name__=='__main__':main()
