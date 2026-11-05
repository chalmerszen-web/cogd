"""Check PCM -> contrast -> all integer heads against independent feature input."""
import argparse,ctypes as C,json,sys
from pathlib import Path
import numpy as np
from wake_host import ROOT,Oracle,build
from wake_prepare import pcm
sys.path.insert(0,str(ROOT/'training/kws'))
from data import Frontend,normalize
def main():
    p=argparse.ArgumentParser();p.add_argument('--models',type=Path,required=True);p.add_argument('--features',type=Path,required=True);p.add_argument('--capture',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    oracle=Oracle(build(a.models,a.out/'host'),268);front=Frontend();norm=json.loads((a.features/'normalization.json').read_text());metadata=json.loads((a.features/'features.json').read_text())
    zero=np.asarray(metadata['contrast_zero_signature'],np.int8);pointer=C.POINTER(C.c_int8);transform=C.CDLL(str((a.features/'contrast.so').resolve()));transform.transform.argtypes=[pointer,pointer,C.c_size_t,C.c_size_t,pointer]
    capture=json.loads(a.capture.read_text(encoding='utf8'));assert capture['complete'];results=[]
    for row in capture['trials'][:6]:
        assert row['source']['split']=='train'
        raw=np.pad(pcm(row['path']),(32768,0)).astype(np.int16);x=np.ascontiguousarray(normalize(front(raw),norm));y=np.empty_like(x)
        transform.transform(x.ctypes.data_as(pointer),y.ctypes.data_as(pointer),1,len(x),zero.ctypes.data_as(pointer))
        feature_scores,feature_events=oracle.features(y);feature_scores=feature_scores[1::2]
        assert oracle.lib.reset(268)==0;score=np.zeros(5,np.int16);pcm_scores=[];pcm_events=[]
        for i,block in enumerate(raw.reshape(-1,512)):
            hit=oracle.lib.feed(block.ctypes.data_as(C.POINTER(C.c_int16)),score.ctypes.data_as(C.POINTER(C.c_int16)));assert hit>=0;pcm_scores.append(score.copy())
            if hit:pcm_events.append((i+1)*512)
        assert np.array_equal(np.asarray(pcm_scores),feature_scores),'PCM frontend and stored feature path differ'
        assert pcm_events==feature_events
        results.append({'id':row['source']['clip_id'],'frames':len(x),'exact':True})
    report={'complete':True,'scope':'TRAIN-only exact causal frontend/three-head/event parity','trials':results};(a.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8');print(json.dumps(report),flush=True)
if __name__=='__main__':main()
