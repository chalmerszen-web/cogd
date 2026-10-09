"""Host C/Python parity; stores fixed PCM and complete expected board traces."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import sys
import time
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from quantize import infer,torch_integer_oracle
from model import verify_streaming

class Trace(C.Structure):
    _fields_=[('logmel',C.c_int16*40),('input',C.c_int8*40),('layer',C.c_int16*265)]

class Trace48(C.Structure):
    _fields_=[('logmel',C.c_int16*40),('input',C.c_int8*40),('layer',C.c_int16*529)]

class Backend:
    def __init__(self,lib,symbol='kws_probe_model',channels=24):
        if channels not in (24,48):raise ValueError('Unregistered C topology')
        self.lib=C.CDLL(str(lib))
        width=getattr(self.lib,'kws_channels',None)
        trace_values=getattr(self.lib,'kws_trace_values',None)
        if width is None or trace_values is None:
            if channels!=24:raise ValueError('Legacy C ABI cannot be used for48 traces')
        elif width()!=channels or trace_values()!=11*channels+1:
            raise ValueError('C/Python trace width mismatch')
        self.trace_type=Trace if channels==24 else Trace48
        self.lib.kws_workspace_size.restype=C.c_size_t
        self.lib.kws_init.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.POINTER(C.c_void_p)]
        self.lib.kws_step_pcm.argtypes=[C.c_void_p,C.POINTER(C.c_int16),C.POINTER(self.trace_type)]
        self.lib.kws_step_features.argtypes=[C.c_void_p,C.POINTER(C.c_int8),C.POINTER(self.trace_type)]
        self.lib.kws_step_features.restype=C.c_int16
        self.lib.kws_reset.argtypes=[C.c_void_p]
        self.memory=C.create_string_buffer(self.lib.kws_workspace_size())
        self.handle=C.c_void_p()
        model=C.c_char.in_dll(self.lib,symbol)
        assert self.lib.kws_init(self.memory,len(self.memory),C.byref(model),C.byref(self.handle))==0
    def pcm(self,pcm):
        trace=self.trace_type(); self.lib.kws_step_pcm(self.handle,pcm.ctypes.data_as(C.POINTER(C.c_int16)),C.byref(trace))
        return trace

def verify_frontend(backend):
    """Independent floating FFT checks frequency placement and energy scale."""
    mel=lambda hz:2595*np.log10(1+hz/700)
    points=700*(10**(np.linspace(mel(125),mel(7500),42)/2595)-1)
    frequencies=np.arange(257)*16000/512
    filters=np.array([np.maximum(0,np.minimum((frequencies-points[i])/(points[i+1]-points[i]),
        (points[i+2]-frequencies)/(points[i+2]-points[i+1]))) for i in range(40)])
    rows=[]
    for hz in (250,1000,4000):
        pcm=(16000*np.sin(2*np.pi*hz*np.arange(512)/16000)).astype(np.int16)
        backend.lib.kws_reset(backend.handle);backend.pcm(pcm[:256]);trace=backend.pcm(pcm[256:])
        power=np.abs(np.fft.rfft(pcm*np.hanning(512))/512)**2
        expected=np.log2(np.maximum(filters@power,1))*256
        peak=int(expected.argmax());actual=np.array(trace.logmel)
        assert int(actual.argmax())==peak,(hz,actual.argmax(),peak)
        assert abs(int(actual[peak])-expected[peak])<8,(hz,actual[peak],expected[peak])
        rows.append(dict(hz=hz,peak_band=peak,log2_error_q8=float(actual[peak]-expected[peak])))
    return rows

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',default='artifacts/kws-phase1');ap.add_argument('--frames',type=int,default=4096)
    ap.add_argument('--model',default='components/kws_c11/generated/probe.json')
    ap.add_argument('--library',default='build-kws-host/libkws.so')
    ap.add_argument('--symbol',default='kws_probe_model')
    ap.add_argument('--channels',type=int,choices=(24,48),default=24)
    args=ap.parse_args();out=ROOT/args.out;out.mkdir(parents=True,exist_ok=True)
    model=json.loads((ROOT/args.model).read_text())
    backend=Backend(ROOT/args.library,args.symbol,args.channels)
    rng=np.random.default_rng(20260921)
    inputs=rng.integers(-128,128,size=(args.frames,40),dtype=np.int8)
    inputs[:4]=np.array([0,127,-128,1],dtype=np.int8)[:,None]
    expected=infer(inputs,model);actual=[]
    np.testing.assert_array_equal(expected,torch_integer_oracle(inputs,model))
    for frame in inputs:
        t=backend.trace_type();backend.lib.kws_step_features(backend.handle,frame.ctypes.data_as(C.POINTER(C.c_int8)),C.byref(t));actual.append(list(t.layer))
    np.testing.assert_array_equal(expected,np.array(actual))
    # Silence, DC limits, Nyquist, impulses, sweeps, modulated tones, noise.
    count=512*256; ts=np.arange(count)/16000
    pcm=np.clip(14000*np.sin(2*np.pi*(180*ts+200*ts*ts))*(.2+.8*np.sin(2*np.pi*.8*ts)**2),-32768,32767).astype(np.int16)
    pcm[:1024]=0;pcm[1024:2048]=32767;pcm[2048:3072]=-32768
    pcm[3072:4096]=np.tile([-32768,32767],512)
    pcm[4096:8192]=0;pcm[4096]=32767;pcm[8192:16384]=rng.integers(-32768,32768,8192,dtype=np.int16)
    backend.lib.kws_reset(backend.handle)
    traces=[];features=[];times=[]
    for frame in pcm.reshape(-1,256):
        start=time.perf_counter_ns();t=backend.pcm(frame);times.append((time.perf_counter_ns()-start)/1000)
        traces.append(dict(logmel=list(t.logmel),input=list(t.input),layer=list(t.layer)))
        features.append(list(t.input))
    expected=infer(features,model)
    np.testing.assert_array_equal(expected,np.array([t['layer'] for t in traces]))
    (out/'fixed.pcm').write_bytes(pcm.astype('<i2').tobytes())
    (out/'fixed-traces.json').write_text(json.dumps(traces,separators=(',',':')))
    report=dict(float_streaming=verify_streaming(args.channels),frontend_sine_checks=verify_frontend(backend),integer_frames=args.frames,integer_values=args.frames*(11*args.channels+1),torch_conv1d_integer_parity=True,
        pcm_frames=len(traces),pcm_sha256=hashlib.sha256(pcm.tobytes()).hexdigest(),workspace=len(backend.memory),
        host_frame_us_p99=float(np.percentile(times,99)),host_frame_us_max=max(times),
        model_json_sha256=hashlib.sha256((ROOT/args.model).read_bytes()).hexdigest(),
        host_library_sha256=hashlib.sha256((ROOT/args.library).read_bytes()).hexdigest(),
        trace_sha256=hashlib.sha256((out/'fixed-traces.json').read_bytes()).hexdigest(),
        trained=bool(model['trained']),model_name=model['name'],passed=True)
    (out/'host-parity.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__': main()
