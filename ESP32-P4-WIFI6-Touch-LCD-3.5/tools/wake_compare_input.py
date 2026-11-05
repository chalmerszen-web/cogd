"""Compare actual P4 PCM/scores to C oracle without replacing live history."""
import argparse,ctypes as C,json,sys,wave
from pathlib import Path
import numpy as np
from wake_host import ROOT,Oracle

def main():
    p=argparse.ArgumentParser();p.add_argument('--capture',type=Path,required=True);p.add_argument('--lib',type=Path,required=True);a=p.parse_args()
    captured=json.loads((a.capture/'report.json').read_text(encoding='utf8'));assert captured['complete']
    oracle=Oracle(a.lib,268);out=[]
    for t in captured['trials']:
        path=ROOT/t['path'];frames=[json.loads(s) for s in (path.parent/'frames.jsonl').read_text().splitlines()]
        with wave.open(str(path),'rb') as w:pcm=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').copy()
        assert oracle.lib.reset(268)==0
        scores=np.zeros(5,np.int16);zero=np.zeros(512,np.int16)
        for _ in range(64):oracle.lib.feed(zero.ctypes.data_as(C.POINTER(C.c_int16)),scores.ctypes.data_as(C.POINTER(C.c_int16)))
        host=[];events=[]
        for i,block in enumerate(pcm.reshape(-1,512)):
            hit=oracle.lib.feed(block.ctypes.data_as(C.POINTER(C.c_int16)),scores.ctypes.data_as(C.POINTER(C.c_int16)))
            assert hit>=0;host.append(scores.copy())
            if hit:events.append(i)
        host=np.asarray(host);device=np.array([f['heads_q8']+f['scores_q8'] for f in frames])
        difference=np.abs(host.astype(int)-device.astype(int));bad=np.flatnonzero(difference.max(1))
        row=dict(id=t['source']['clip_id'],path=t['path'],exact=bool(not len(bad)),mismatched_frames=len(bad),
                 max_difference=difference.max(0).tolist(),host_hits=events,device_hits=[f['sequence'] for f in frames if f['flags']&1],
                 host_max=host.max(0).tolist(),device_max=device.max(0).tolist(),
                 first_mismatches=[dict(frame=int(i),host=host[i].tolist(),device=device[i].tolist()) for i in bad[:3]])
        np.savez_compressed(path.parent/'score-comparison.npz',host=host,device=device,pcm=pcm)
        out.append(row);print(json.dumps(row),flush=True)
    (a.capture/'comparison.json').write_text(json.dumps(out,indent=2),encoding='utf8')

if __name__=='__main__':main()
