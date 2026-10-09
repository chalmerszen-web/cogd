"""Exact shared-frontend fusion oracle, export fixed board trace expectations."""
import ctypes as C
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
from parity import Backend,Trace
sys.path.insert(0,str(ROOT/'training/kws'))
from evaluate import events

class Event(C.Structure):
    _fields_=[('score_q8',C.c_int16),('detected',C.c_bool),('sample_end',C.c_uint64)]


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--pair',choices=('ef','ek'),default='ef')
    args=parser.parse_args()
    out=ROOT/'artifacts/kws-phase5'/('fusion-'+args.pair+'-smooth3')
    path=ROOT/('build-kws-fusion-host' if args.pair=='ef' else 'build-kws-fusion-ek-host')/'libkws.so'
    lib=C.CDLL(str(path));lib.kws_fusion_size.restype=C.c_size_t
    lib.kws_fusion_init.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.c_void_p,C.POINTER(C.c_void_p)]
    lib.kws_fusion_step_pcm.argtypes=[C.c_void_p,C.POINTER(C.c_int16),C.c_uint64,C.POINTER(Event),C.POINTER(Trace)]
    lib.kws_fusion_feed_512.argtypes=[C.c_void_p,C.POINTER(C.c_int16),C.c_uint64,C.POINTER(Event)]
    lib.kws_fusion_scores.argtypes=[C.c_void_p,C.POINTER(C.c_int16)]
    lib.kws_fusion_threshold.argtypes=[C.c_void_p,C.c_int16];lib.kws_fusion_reset.argtypes=[C.c_void_p]
    a=Backend(path,'kws_trained_model');b=Backend(path,'kws_secondary_model')
    memory=C.create_string_buffer(lib.kws_fusion_size());handle=C.c_void_p()
    first=C.c_char.in_dll(lib,'kws_trained_model');second=C.c_char.in_dll(lib,'kws_secondary_model')
    assert not lib.kws_fusion_init(memory,len(memory),C.byref(first),C.byref(second),C.byref(handle))
    assert len(memory)<=20480
    threshold=json.loads((out/'threshold.json').read_text())['quantized'];lib.kws_fusion_threshold(handle,threshold)
    raw=(out/'adapt-e-parity/fixed.pcm').read_bytes();pcm=np.frombuffer(raw,dtype='<i2').copy()
    values=[];event_scores=[];recent=[];history=[]
    for i,frame in enumerate(pcm.reshape(-1,256)):
        ta=a.pcm(frame);tb=b.pcm(frame);trace=Trace();event=Event()
        assert not lib.kws_fusion_step_pcm(handle,frame.ctypes.data_as(C.POINTER(C.c_int16)),(i+1)*256,C.byref(event),C.byref(trace))
        assert bytes(trace)==bytes(ta)
        members=(C.c_int16*2)();lib.kws_fusion_scores(handle,members)
        assert list(members)==[ta.layer[264],tb.layer[264]]
        if i%2:
            combined=int((int(members[0])+int(members[1]))/2);recent.append(combined)
            score=int(sum(recent[-3:])/min(3,len(recent)))
            event_scores.extend([0,score]);expected=events(np.array(event_scores),threshold)
            assert event.score_q8==score and event.detected==bool(expected and expected[-1]==(i+1)*256)
        else:assert not event.detected
        values.append(dict(seq=i,members=list(members),score=event.score_q8,detected=bool(event.detected)))
        history.append((event.score_q8,bool(event.detected)))
    # The live 512-sample API must match USB's 256-sample stepping exactly.
    lib.kws_fusion_reset(handle)
    for i,frame in enumerate(pcm.reshape(-1,512)):
        event=Event();assert not lib.kws_fusion_feed_512(handle,frame.ctypes.data_as(C.POINTER(C.c_int16)),(i+1)*512,C.byref(event))
        assert (event.score_q8,bool(event.detected))==history[2*i+1]
    (out/'fixed.pcm').write_bytes(raw)
    (out/'fusion-traces.json').write_text(json.dumps(values,separators=(',',':')))
    report=dict(passed=True,frames=len(values),workspace=len(memory),library_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        pcm_sha256=hashlib.sha256(raw).hexdigest(),threshold_q8=threshold,board_verified=False)
    (out/'fusion-host-parity.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))


if __name__=='__main__':main()
