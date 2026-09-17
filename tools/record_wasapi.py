"""Bounded local microphone diagnostic with per-stream Windows RAW selection.

Uses the already pinned SoundCard 0.4.6 device/format wrappers, without changing
the library, endpoint settings or monitoring. RAW may retain always-on endpoint
effects; enumerate advertised effects instead of claiming an unprocessed ADC.
"""
import argparse
import ctypes as ct
import hashlib
from importlib.metadata import version
import json
from pathlib import Path
import time
import uuid
import wave
import numpy as np
import soundcard as sc
from soundcard import mediafoundation as mf


class Guid(ct.Structure):
    _fields_=[('a',ct.c_uint32),('b',ct.c_uint16),('c',ct.c_uint16),('d',ct.c_ubyte*8)]
    @classmethod
    def parse(cls,value): return cls.from_buffer_copy(uuid.UUID(value).bytes_le)


class Properties(ct.Structure):
    _fields_=[('size',ct.c_uint32),('offload',ct.c_int32),('category',ct.c_int32),('options',ct.c_uint32)]


class Effect(ct.Structure):
    _fields_=[('id',Guid),('can_set',ct.c_int32),('state',ct.c_int32)]


def call(pointer,index,args,*values):
    vtable=ct.cast(pointer,ct.POINTER(ct.POINTER(ct.c_void_p))).contents
    return ct.WINFUNCTYPE(ct.c_long,ct.c_void_p,*args)(vtable[index])(pointer,*values)


def checked(hr):
    if hr<0: raise RuntimeError(f'Windows audio HRESULT 0x{hr&0xffffffff:08x}')


def address(pointer): return int(mf._ffi.cast('uintptr_t',pointer[0]))


def set_mode(pointer,raw):
    # audioclient.h: IAudioClient2 extends the 15-method IAudioClient vtable.
    iid=Guid.parse('726778cd-f60a-4eda-82de-e47610cd78aa');extra=ct.c_void_p()
    checked(call(address(pointer),0,[ct.POINTER(Guid),ct.POINTER(ct.c_void_p)],ct.byref(iid),ct.byref(extra)))
    try:
        props=Properties(ct.sizeof(Properties),0,0,int(raw))
        checked(call(extra,16,[ct.POINTER(Properties)],ct.byref(props)))
    finally: call(extra,2,[])


def effects(pointer):
    iid=Guid.parse('4460b3ae-4b44-4527-8676-7548a8acd260');manager=ct.c_void_p()
    hr=call(address(pointer),14,[ct.POINTER(Guid),ct.POINTER(ct.c_void_p)],ct.byref(iid),ct.byref(manager))
    if hr<0:return dict(available=False,hresult=f'0x{hr&0xffffffff:08x}')
    values=ct.POINTER(Effect)();count=ct.c_uint32()
    ole=ct.WinDLL('ole32');ole.CoTaskMemFree.argtypes=[ct.c_void_p];ole.CoTaskMemFree.restype=None
    try:
        checked(call(manager,5,[ct.POINTER(ct.POINTER(Effect)),ct.POINTER(ct.c_uint32)],ct.byref(values),ct.byref(count)))
        if count.value>64:raise RuntimeError('Unexpected effect count')
        names={'6f64adbe':'echo_cancellation','6f64adbf':'noise_suppression','6f64adc0':'automatic_gain'}
        rows=[]
        for v in values[:count.value]:
            ident=str(uuid.UUID(bytes_le=bytes(v.id)))
            rows.append(dict(id=ident,name=names.get(ident[:8],'other'),can_set=bool(v.can_set),state=v.state))
        return dict(available=True,items=rows)
    finally:
        if values:ole.CoTaskMemFree(values)
        call(manager,2,[])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--seconds',type=float,default=6)
    p.add_argument('--buffer-ms',type=int,choices=(100,500),default=500,
                   help='Host read-stall headroom; packet-gap validation remains strict')
    p.add_argument('--mode',choices=('default','raw'),required=True)
    p.add_argument('--device',default='麦克风 (Misiom-Shooter)')
    p.add_argument('--mono',action='store_true',help='Select channel zero only if both native channels are identical')
    p.add_argument('--stop-file',type=Path,help='A parent may create this fresh path to stop a bounded capture cleanly')
    a=p.parse_args()
    meta_path=a.output.with_suffix('.wasapi.json')
    if not 0<a.seconds<=60 or a.output.exists() or meta_path.exists() or (a.stop_file and a.stop_file.exists()):p.error('Use fresh paths and 0..60 seconds')
    assert version('SoundCard')=='0.4.6' and ct.sizeof(Properties)==16 and ct.sizeof(Effect)==24
    choices=[m for m in sc.all_microphones(include_loopback=False) if m.name==a.device]
    if len(choices)!=1:raise RuntimeError('Expected exactly one microphone with the specified full name')
    mic=choices[0];channels=mic.channels;rate=48000
    if not 1<=channels<=2:raise RuntimeError('Unexpected microphone channel count')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    meta=dict(device=mic.name,device_id=mic.id,mode=a.mode,rate=rate,channels=1 if a.mono else channels,native_channels=channels,seconds=a.seconds,
              complete=False,blocks=[],frames=0,clipped_float_samples=0,started=None,
              scope='Per-stream properties only; no endpoint writes or microphone monitoring')
    def save():meta_path.write_text(json.dumps(meta,indent=2,ensure_ascii=False),encoding='utf8')
    ptr=capture=None;started=False
    try:
        ptr=mic._audio_client();set_mode(ptr,a.mode=='raw');meta['set_properties_hresult']='0x00000000'
        native=mf._ffi.new('WAVEFORMATEXTENSIBLE**')
        checked(ptr[0][0].lpVtbl.GetMixFormat(ptr[0],native))
        try:
            meta['native_rate']=native[0][0].Format.nSamplesPerSec
            if meta['native_rate']!=rate:raise RuntimeError('Use the verified native 48-kHz microphone format')
        finally:mf._ole32.CoTaskMemFree(native[0])
        recorder=mf._Recorder(ptr,rate,channels,48*a.buffer_ms,False)
        meta['buffer_ms']=a.buffer_ms;meta['buffer_frames']=recorder.buffersize
        if meta['buffer_frames']<48*a.buffer_ms:raise RuntimeError('Actual capture buffer is smaller than requested')
        meta['effects']=effects(ptr)
        capture=recorder._capture_client();recorder._ppCaptureClient=capture
        checked(ptr[0][0].lpVtbl.Start(ptr[0]));started=True
        meta['started']=time.monotonic();save()
        target=round(a.seconds*rate)
        with wave.open(str(a.output),'wb') as wav:
            wav.setparams((meta['channels'],2,rate,0,'NONE','NONE'))
            while meta['frames']<target:
                if a.stop_file and a.stop_file.exists():meta['stop_reason']='parent_request';break
                if time.monotonic()-meta['started']>a.seconds+5:raise TimeoutError('Microphone did not deliver bounded samples')
                if not recorder._capture_available_frames():time.sleep(.002);continue
                data=mf._ffi.new('BYTE**');n=mf._ffi.new('UINT32*');flags=mf._ffi.new('DWORD*')
                pos=mf._ffi.new('UINT64*');qpc=mf._ffi.new('UINT64*')
                checked(capture[0][0].lpVtbl.GetBuffer(capture[0],data,n,flags,pos,qpc))
                if not n[0]:time.sleep(.002);continue
                try:
                    take=min(n[0],target-meta['frames'])
                    if flags[0]&2:x=np.zeros(take*channels)
                    else:x=np.frombuffer(mf._ffi.buffer(data[0],n[0]*4*channels),'<f4')[:take*channels]
                    if not np.isfinite(x).all():raise RuntimeError('Non-finite microphone PCM')
                    if a.mono and channels>1:
                        x=x.reshape(-1,channels)
                        if not np.array_equal(x[:,0],x[:,1]):raise RuntimeError('Native microphone channels differ; refusing lossy mono selection')
                        x=x[:,0]
                    meta['clipped_float_samples']+=int(np.count_nonzero(abs(x)>=1))
                    wav.writeframesraw(np.clip(np.rint(x*32768),-32768,32767).astype('<i2').tobytes())
                    meta['blocks'].append(dict(at=meta['frames'],frames=n[0],written=take,flags=flags[0],position=int(pos[0]),qpc_100ns=int(qpc[0]),host_time=time.monotonic()))
                    meta['frames']+=take
                finally:checked(capture[0][0].lpVtbl.ReleaseBuffer(capture[0],n[0]))
        if not meta['frames']:raise RuntimeError('No microphone frames captured')
        blocks=meta['blocks']
        meta['discontinuities_after_first']=sum(bool(b['flags']&1) for b in blocks[1:])
        meta['timestamp_errors']=sum(bool(b['flags']&4) for b in blocks)
        meta['position_gaps']=sum(b['position']!=prev['position']+prev['frames'] for prev,b in zip(blocks,blocks[1:]))
        meta.setdefault('stop_reason','duration_bound')
        meta['complete']=not any(meta[k] for k in ('discontinuities_after_first','timestamp_errors','position_gaps'))
        if not meta['complete']:raise RuntimeError('Microphone discontinuity or timestamp error; evidence retained')
        meta['sha256']=hashlib.sha256(a.output.read_bytes()).hexdigest()
    except BaseException as e:
        meta['error']=str(e);raise
    finally:
        try:
            if started:checked(ptr[0][0].lpVtbl.Stop(ptr[0]))
        finally:
            if capture is not None:mf._com.release(capture)
            if ptr is not None:mf._com.release(ptr)
            meta['finished']=time.monotonic();save()
    print(json.dumps({k:v for k,v in meta.items() if k!='blocks'},ensure_ascii=False))


if __name__=='__main__':
    try:main()
    finally:
        # Release SoundCard's successful COM initialization before Python tears
        # down its DLL global; the pinned library destructor otherwise runs late.
        if mf._com.com_loaded:
            mf._ole32.CoUninitialize();mf._com.com_loaded=False
