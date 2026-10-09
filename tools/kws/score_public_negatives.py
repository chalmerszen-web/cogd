"""Finite original-PCM check of new public TRAIN speech with a verified C backend.

No audio collection, network, training, threshold selection or deployment.
This intentionally does not model the speaker/microphone path.
"""
import argparse
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import time
import wave

import numpy as np

ROOT=Path(__file__).resolve().parents[2]
PRIME=32768
TAIL=24000

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

class Event(C.Structure):
    _fields_=[('score_q8',C.c_int16),('detected',C.c_bool),('sample_end',C.c_uint64)]

def detections(scores,threshold):
    """Independent two-of-three, sample-clock cooldown check, including prime."""
    votes=0; cooldown=0; result=[]
    for block,score in enumerate(scores,1):
        end=block*512;votes=((votes<<1)|int(score>=threshold))&7
        if block>=64 and end>=cooldown and votes.bit_count()>=2:
            result.append(end);cooldown=end+24000;votes=0
    return result

class Fusion:
    def __init__(self,path,threshold):
        self.lib=C.CDLL(str(path));self.threshold=threshold
        self.lib.kws_fusion_size.restype=C.c_size_t
        self.lib.kws_fusion_init.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_void_p,C.POINTER(C.c_void_p)]
        self.lib.kws_fusion_feed_512.argtypes=[C.c_void_p,C.POINTER(C.c_int16),C.c_uint64,C.POINTER(Event)]
        self.lib.kws_fusion_threshold.argtypes=[C.c_void_p,C.c_int16]
        self.lib.kws_fusion_scores.argtypes=[C.c_void_p,C.POINTER(C.c_int16)]
        self.first=C.c_char.in_dll(self.lib,'kws_trained_model')
        self.second=C.c_char.in_dll(self.lib,'kws_secondary_model')
    def score(self,pcm):
        size=self.lib.kws_fusion_size();memory=C.create_string_buffer(size+7)
        pointer=(C.addressof(memory)+7)&~7;handle=C.c_void_p()
        if self.lib.kws_fusion_init(pointer,size,C.byref(self.first),C.byref(self.second),C.byref(handle)):
            raise ValueError('Incompatible or unaligned verified C model')
        event=Event();zero=np.zeros(512,dtype=np.int16);scores=[]
        for block in range(64):
            if self.lib.kws_fusion_feed_512(handle,zero.ctypes.data_as(C.POINTER(C.c_int16)),(block+1)*512,C.byref(event)):
                raise RuntimeError('C prime failed')
            assert not event.detected
            scores.append(event.score_q8)
        self.lib.kws_fusion_threshold(handle,self.threshold)
        signal=np.pad(pcm,(0,TAIL+(-len(pcm)-TAIL)%512)).astype(np.int16)
        members=[];events=[];pair=(C.c_int16*2)()
        for block,frame in enumerate(signal.reshape(-1,512),1):
            end=PRIME+block*512
            if self.lib.kws_fusion_feed_512(handle,frame.ctypes.data_as(C.POINTER(C.c_int16)),end,C.byref(event)):
                raise RuntimeError('C feed failed')
            assert event.sample_end==end
            self.lib.kws_fusion_scores(handle,pair);members.append(list(pair));scores.append(event.score_q8)
            if event.detected:events.append(end)
        assert events==detections(scores,self.threshold),'C event/sample-clock disagrees with independent detector'
        return np.asarray(scores[64:],dtype=np.int16),np.asarray(members,dtype=np.int16),[e-PRIME for e in events]

def main():
    parser=argparse.ArgumentParser()
    for name in ('manifest','library','parity','out'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--max-seconds',type=int,default=120)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    parity=json.loads(args.parity.read_text(encoding='utf8'))
    assert parity['complete'] and parity['all_frames_equal'] and parity['frames_verified']==6387
    assert sha(args.library)==parity['library_sha256']
    rows=[json.loads(line) for line in args.manifest.read_text(encoding='utf8').splitlines() if line.strip()]
    assert len(rows)==128 and len({r['original_recording_id'] for r in rows})==128
    assert all(r['split']=='train' and r['label']==0 and r['source_dataset']=='google/fleurs' for r in rows)
    threshold=round(math.log(.740/.260)*256)
    assert threshold==parity['threshold_q8']==268
    plan=dict(threshold=740,threshold_q8=threshold,prime_samples=PRIME,tail_samples=TAIL,
        max_seconds=args.max_seconds,manifest_sha256=sha(args.manifest),library_sha256=sha(args.library),
        verified_parity_sha256=sha(args.parity),script_sha256=sha(Path(__file__)),rows=128,
        source_pcm_only=True,test_read=False,training=False,device_or_cloud=False,
        limitations='Public TRAIN recordings, possibly same TRAIN speakers; original source PCM with silence prime and tail, not real microphone input, no field FAR/human positive acceptance.')
    (args.out/'plan.json').write_text(json.dumps(plan,indent=2)+'\n',encoding='utf8')
    report=dict(plan,complete=False,rows=[]);started=time.monotonic()
    try:
        backend=Fusion(args.library,threshold)
        for index,row in enumerate(rows):
            if time.monotonic()-started>args.max_seconds:raise TimeoutError('Declared C scoring budget exhausted')
            path=(ROOT/row['path']).resolve()
            assert path.is_relative_to(ROOT)
            with wave.open(str(path),'rb') as w:
                assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(1,2,16000)
                raw=w.readframes(w.getnframes())
            assert hashlib.sha256(raw).hexdigest()==row['pcm_sha256']
            pcm=np.frombuffer(raw,dtype='<i2').copy()
            scores,members,events=backend.score(pcm)
            np.savez_compressed(args.out/f'{index:03}.npz',scores=scores,members=members)
            report['rows'].append(dict(clip_id=row['clip_id'],language=row['language'],
                pcm_sha256=row['pcm_sha256'],samples=len(pcm),events_samples=events,
                peak_q8=int(scores.max()),all_source_samples_preserved=True))
            if (index+1)%32==0:print(json.dumps(dict(scored=index+1)),flush=True)
        report['summary']={lang:dict(clips=sum(r['language']==lang for r in report['rows']),
            triggered=sum(r['language']==lang and bool(r['events_samples']) for r in report['rows']),
            events=sum(len(r['events_samples']) for r in report['rows'] if r['language']==lang)) for lang in ('zh','yue')}
        report['complete']=True
    except Exception as error:
        report['error']=repr(error);raise
    finally:
        report['elapsed_seconds']=time.monotonic()-started
        (args.out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(report['summary']),flush=True)

if __name__=='__main__':main()
