"""Bounded PCM playback to an explicitly selected Windows output endpoint."""
import argparse
import array
import ctypes as c
import json
import math
from pathlib import Path
import time
import wave

class Format(c.Structure):
    _fields_=[('tag',c.c_ushort),('channels',c.c_ushort),('rate',c.c_uint),
              ('bytes_sec',c.c_uint),('align',c.c_ushort),('bits',c.c_ushort),('extra',c.c_ushort)]

class Header(c.Structure):
    _fields_=[('data',c.c_void_p),('length',c.c_uint),('recorded',c.c_uint),('user',c.c_size_t),
              ('flags',c.c_uint),('loops',c.c_uint),('next',c.c_void_p),('reserved',c.c_size_t)]

class OutputCaps(c.Structure):
    _fields_=[('manufacturer',c.c_ushort),('product',c.c_ushort),('version',c.c_uint),
              ('name',c.c_wchar*32),('formats',c.c_uint),('channels',c.c_ushort),
              ('reserved',c.c_ushort),('support',c.c_uint)]

def output_name(identifier):
    api=c.windll.winmm
    api.waveOutGetDevCapsW.argtypes=[c.c_size_t,c.POINTER(OutputCaps),c.c_uint]
    caps=OutputCaps()
    code=api.waveOutGetDevCapsW(identifier,c.byref(caps),c.sizeof(caps))
    if code: raise RuntimeError('waveOut capability error '+str(code))
    return caps.name

def output_devices():
    return [(i,output_name(i)) for i in range(c.windll.winmm.waveOutGetNumDevs())]

def play(path,device=None,gain=.35,preroll_ms=0):
    if not 0 <= gain <= 1:
        raise ValueError('Gain must be 0..1')
    if not 0 <= preroll_ms <= 1000:
        raise ValueError('Preroll must be 0..1000 ms')
    candidates=[(i,name) for i,name in output_devices() if i==device or (device is None and 'Misiom-Shooter' in name)]
    if len(candidates)!=1:
        raise RuntimeError('Expected exactly one matching output endpoint; no audio submitted')
    device,device_name=candidates[0]
    with wave.open(str(path),'rb') as wav:
        if wav.getsampwidth()!=2 or wav.getnframes()/wav.getframerate()>90:
            raise ValueError('Expected bounded PCM16 WAV')
        rate, channels = wav.getframerate(),wav.getnchannels()
        samples=array.array('h',wav.readframes(wav.getnframes()))
    for i in range(len(samples)):
        samples[i]=round(samples[i]*gain)
    padding=rate*preroll_ms//1000
    result=_submit(bytes(padding*channels*2)+samples.tobytes(),rate,channels,device,device_name)
    return dict(result,source_started=result['started']+padding/rate,sample_rate=rate,
                channels=channels,samples=len(samples)//channels,preroll_samples=padding)


def _submit(payload,rate,channels,device,device_name,loops=1,stop=None,ready=None):
    data=c.create_string_buffer(payload)
    fmt=Format(1,channels,rate,rate*channels*2,channels*2,16,0)
    hdr=Header(c.cast(data,c.c_void_p),len(payload))
    if loops>1:
        hdr.flags=4|8  # WHDR_BEGINLOOP | WHDR_ENDLOOP; finite driver-side repeats.
        hdr.loops=loops
    handle=c.c_void_p(); api=c.windll.winmm
    api.waveOutOpen.argtypes=[c.POINTER(c.c_void_p),c.c_uint,c.POINTER(Format),c.c_size_t,c.c_size_t,c.c_uint]
    for name in ('waveOutPrepareHeader','waveOutWrite','waveOutUnprepareHeader'):
        getattr(api,name).argtypes=[c.c_void_p,c.POINTER(Header),c.c_uint]
    api.waveOutReset.argtypes=api.waveOutClose.argtypes=[c.c_void_p]
    def check(code):
        if code: raise RuntimeError('waveOut error '+str(code))
    check(api.waveOutOpen(c.byref(handle),device,c.byref(fmt),0,0,0))
    prepared=False
    try:
        # Enumeration can change between selecting an index and opening it.
        # Verify the actual handle before submitting any test samples.
        if output_name(handle.value)!=device_name:
            raise RuntimeError('Output endpoint changed while opening; no audio submitted')
        check(api.waveOutPrepareHeader(handle,c.byref(hdr),c.sizeof(hdr))); prepared=True
        started=time.monotonic()
        check(api.waveOutWrite(handle,c.byref(hdr),c.sizeof(hdr)))
        if ready: ready.set()
        deadline=started+len(payload)*loops/(channels*2*rate)+5
        while not hdr.flags&1:
            if stop and stop.is_set(): break
            if time.monotonic()>deadline: raise TimeoutError('Output endpoint did not finish')
            time.sleep(.01)
        return dict(started=started,finished=time.monotonic(),device_index=device,device_name=device_name)
    finally:
        api.waveOutReset(handle)
        if prepared: api.waveOutUnprepareHeader(handle,c.byref(hdr),c.sizeof(hdr))
        api.waveOutClose(handle)


def silence(seconds,device_name,stop,ready):
    """Hold the named output clock with finite all-zero PCM, without changing volume."""
    if not math.isfinite(seconds) or not 0<seconds<=95:
        raise ValueError('Silent output must be bounded to 0..95 seconds')
    candidates=[(i,name) for i,name in output_devices() if name==device_name]
    if len(candidates)!=1:
        raise RuntimeError('Expected exactly one matching silent output endpoint')
    device,device_name=candidates[0]
    # WinMM repeats a fixed 100-ms zero buffer. No Python audio callback and
    # no changes to the separately submitted test sources or endpoint volume.
    return _submit(bytes(4800*2*2),48000,2,device,device_name,
                   loops=math.ceil(seconds*10),stop=stop,ready=ready)

if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('path'); p.add_argument('--device',type=int,help='Explicit index override; default resolves the Misiom-Shooter name each time')
    p.add_argument('--gain',type=float,default=.35)
    p.add_argument('--trace',type=Path)
    p.add_argument('--preroll-ms',type=int,default=0)
    args=p.parse_args(); result=play(args.path,args.device,args.gain,args.preroll_ms)
    if args.trace: args.trace.write_text(json.dumps(result,indent=2),encoding='utf8')
