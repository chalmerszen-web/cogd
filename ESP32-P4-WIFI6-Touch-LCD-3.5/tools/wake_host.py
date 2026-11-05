"""Exact host C11 oracle for a frozen P4 24/24/48 model directory."""
import argparse, ctypes as C, hashlib, json, subprocess, sys, wave
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
KWS=ROOT/'components/kws_c11'
WRAPPER=r'''
#include "kws_verified.h"
#include "kws_temporal_owner.h"
#include <stddef.h>
#include <stdint.h>
extern const kws_model_t kws_verified_primary,kws_verified_secondary,kws_trained_model;
static union { max_align_t alignment; unsigned char bytes[65536]; } memory;
static kws_verified_t *state;
static uint64_t end;
int reset(int threshold) {
  end=0;
  int error=kws_verified_init(memory.bytes,sizeof(memory.bytes),&kws_verified_primary,&kws_verified_secondary,&kws_trained_model,&state);
  if(!error) kws_verified_threshold(state,(int16_t)threshold);
  return error;
}
int feed(const int16_t *pcm,int16_t *scores) {
  kws_event_t event; end+=512;
  if(kws_verified_feed_512_armed(state,pcm,end,true,&event))return -1;
  kws_verified_scores(state,scores,scores+3);
  return event.detected;
}
int feature(const int8_t *input,int16_t *scores) {
  kws_event_t event; end+=256;
  if(kws_verified_step_features_armed(state,input,end,true,&event,NULL))return -1;
  kws_verified_scores(state,scores,scores+3);
  return event.detected;
}
size_t workspace(void) {return kws_verified_size();}
static kws_temporal_owner_t replay_owner;
void decision_reset(int threshold) {
  kws_temporal_owner_reset(&replay_owner,(int16_t)threshold);
  for(unsigned i=1;i<=64;++i)kws_temporal_owner_step(&replay_owner,-32768,-32768,(uint64_t)i*512,true);
}
int decision_step(int primary,int verifier,uint64_t sample_end) {
  return kws_temporal_owner_step(&replay_owner,(int16_t)primary,(int16_t)verifier,sample_end,true);
}
'''

def build(models,out):
    models=Path(models).resolve();out=Path(out).resolve()
    out.mkdir(parents=True,exist_ok=True)
    wrapper=out/'wrapper.c';wrapper.write_text(WRAPPER,encoding='utf8')
    code=['kws_quant.c','kws_detector.c','kws_nn.c','kws_frontend.c','kws_fft.c','kws_pcen.c',
          'kws_temporal_owner.c','kws_verified.c','generated/tables.c']
    sources=[KWS/c for c in code]+[models/(n+'.c') for n in ('first','second','verifier')]+[wrapper]
    policy={}
    if (models/'runtime.json').exists():
        policy=json.loads((models/'runtime.json').read_text(encoding='utf8'))
        owner=ROOT/policy['temporal_source']
        assert hashlib.sha256(owner.read_bytes()).hexdigest()==policy['sha256']
        if policy.get('temporal_header_source'):
            header=ROOT/policy['temporal_header_source']
            assert hashlib.sha256(header.read_bytes()).hexdigest()==policy['temporal_header_sha256']
            wrapper.write_text(WRAPPER.replace('"kws_temporal_owner.h"','"'+header.as_posix()+'"'),encoding='utf8')
        sources=[owner if p==KWS/'kws_temporal_owner.c' else p for p in sources]
        if policy.get('wide_runtime_source'):
            wide=ROOT/policy['wide_runtime_source']
            assert hashlib.sha256(wide.read_bytes()).hexdigest()==policy['wide_runtime_sha256']
            header=ROOT/policy['wide_header_source']
            assert hashlib.sha256(header.read_bytes()).hexdigest()==policy['wide_header_sha256']
            sources=[wide if p==KWS/'kws_verified.c' else p for p in sources]
        if policy.get('contrast_header_source'):
            header=ROOT/policy['contrast_header_source']
            assert hashlib.sha256(header.read_bytes()).hexdigest()==policy['contrast_header_sha256']
        if policy.get('private_kernel_dir'):
            kernel=ROOT/policy['private_kernel_dir']
            for name,digest in policy['private_kernel_hashes'].items():assert hashlib.sha256((kernel/name).read_bytes()).hexdigest()==digest
            sources=[kernel/p.name if p in (KWS/'kws_nn.c',KWS/'kws_frontend.c') else p for p in sources]
    lib=out/'libwake.so'
    cmd=['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC','-DKWS_BUILD_CHANNELS=48','-DKWS_ALLOW_MIXED24=1',
         '-I'+str(KWS/'include'),'-I'+str(KWS),'-I'+str(ROOT/'third_party/kissfft'),*map(str,sources),'-lm','-o',str(lib)]
    if policy.get('private_kernel_dir'):
        cmd[cmd.index('-DKWS_BUILD_CHANNELS=48')]='-DKWS_BUILD_CHANNELS=96'
        cmd[1:1]=['-I'+str(kernel/'include'),'-I'+str(kernel)]
    subprocess.run(cmd,check=True)
    (out/'build.json').write_text(json.dumps({'command':cmd,'hashes':{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}},indent=2),encoding='utf8')
    return lib

class Head96:
    """Private P4 trace ABI; leaves the shared24/48 host backend unchanged."""
    class Trace(C.Structure):
        _fields_=[('logmel',C.c_int16*40),('input',C.c_int8*40),('layer',C.c_int16*1057)]
    def __init__(self,library,symbol):
        self.lib=C.CDLL(str(library));self.trace_type=self.Trace
        assert self.lib.kws_channels()==96 and self.lib.kws_trace_values()==1057
        self.lib.kws_workspace_size.restype=C.c_size_t
        self.lib.kws_init.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p,C.POINTER(C.c_void_p)]
        self.lib.kws_step_features.argtypes=[C.c_void_p,C.POINTER(C.c_int8),C.POINTER(self.Trace)]
        self.memory=C.create_string_buffer(self.lib.kws_workspace_size());self.handle=C.c_void_p();model=C.c_char.in_dll(self.lib,symbol)
        assert self.lib.kws_init(self.memory,len(self.memory),C.byref(model),C.byref(self.handle))==0

class Oracle:
    def __init__(self,lib,threshold=268):
        self.lib=C.CDLL(str(Path(lib).resolve()));self.threshold=threshold
        self.lib.reset.argtypes=[C.c_int];self.lib.reset.restype=C.c_int
        self.lib.feed.argtypes=[C.POINTER(C.c_int16),C.POINTER(C.c_int16)];self.lib.feed.restype=C.c_int
        self.lib.feature.argtypes=[C.POINTER(C.c_int8),C.POINTER(C.c_int16)];self.lib.feature.restype=C.c_int
        self.lib.workspace.restype=C.c_size_t
    def pcm(self,pcm):
        assert self.lib.reset(self.threshold)==0
        pcm=np.pad(np.asarray(pcm,dtype=np.int16),(40960,16384))
        pcm=np.pad(pcm,(0,(-len(pcm))%512)).astype(np.int16)
        scores=np.zeros((len(pcm)//512,5),dtype=np.int16);events=[]
        for i,block in enumerate(pcm.reshape(-1,512)):
            hit=self.lib.feed(block.ctypes.data_as(C.POINTER(C.c_int16)),scores[i].ctypes.data_as(C.POINTER(C.c_int16)))
            assert hit>=0
            if hit:events.append((i+1)*512-40960)
        return scores,events
    def wav(self,path):
        with wave.open(str(path),'rb') as w:
            assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(1,2,16000)
            pcm=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2')
        return self.pcm(pcm)
    def features(self,x):
        assert self.lib.reset(self.threshold)==0
        x=np.ascontiguousarray(x,dtype=np.int8);scores=np.zeros((len(x),5),dtype=np.int16);events=[]
        for i,frame in enumerate(x):
            hit=self.lib.feature(frame.ctypes.data_as(C.POINTER(C.c_int8)),scores[i].ctypes.data_as(C.POINTER(C.c_int16)))
            assert hit>=0
            if hit:events.append((i+1)*256)
        return scores,events

def diagnose(captures,lib,out):
    oracle=Oracle(lib);report={'workspace_host':oracle.lib.workspace(),'threshold_q8':oracle.threshold,'trials':[]}
    for t in json.loads(captures.read_text(encoding='utf8'))['trials']:
        row={'source':t['source']}
        for name,path in [('direct',ROOT/t['source']['path']),('microphone',ROOT/t['path'])]:
            scores,events=oracle.wav(path)
            row[name]={'events':events,'head_max':scores[80:].max(0).tolist(),'primary_verifier_max':scores[80:,3:].max(0).tolist()}
        report['trials'].append(row)
    out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    for t in report['trials']: print(t['source']['clip_id'],t['direct']['events'],t['microphone']['events'],t['direct']['primary_verifier_max'],t['microphone']['primary_verifier_max'])

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--models',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--captures',type=Path);a=p.parse_args()
    lib=build(a.models,a.out)
    if a.captures:diagnose(a.captures,lib,a.out/'diagnosis.json')
