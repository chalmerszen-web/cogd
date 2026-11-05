"""Apply one causal integer frontend to TRAIN features; never load final audio."""
import argparse,ctypes as C,hashlib,json,subprocess,sys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
HEADER=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/board/kws_contrast.h'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--previous',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    a.previous=a.previous.resolve();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    wrapper=a.out/'contrast.c';wrapper.write_text('#include "'+HEADER.as_posix()+'"\n#include <stddef.h>\nvoid transform(const int8_t *x,int8_t *y,size_t clips,size_t frames,const int8_t *zero){for(size_t c=0;c<clips;++c){p4_contrast_t s={0};for(size_t t=0;t<frames;++t)p4_contrast_step(&s,x+(c*frames+t)*40,zero,y+(c*frames+t)*40);}}\n',encoding='utf8')
    lib=a.out/'contrast.so';subprocess.run(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',str(wrapper),'-o',str(lib)],check=True)
    dll=C.CDLL(str(lib));pointer=C.POINTER(C.c_int8);dll.transform.argtypes=[pointer,pointer,C.c_size_t,C.c_size_t,pointer]
    norm=json.loads((a.previous/'normalization.json').read_text());value=-np.array(norm['mean_q8'],np.int64)*norm['inverse_std_q12'];zero=np.clip(np.sign(value)*((np.abs(value)+16384)//32768),-128,127).astype(np.int8)
    # Check the independently expressed vector recurrence against the C kernel.
    rng=np.random.default_rng(110196);probe=rng.integers(-128,128,(3,400,40),dtype=np.int8);probe[:,:128]=zero;actual=np.empty_like(probe)
    dll.transform(probe.ctypes.data_as(pointer),actual.ctypes.data_as(pointer),3,400,zero.ctypes.data_as(pointer))
    expected=np.empty_like(probe)
    for n in range(3):
        mean=np.zeros(40,np.int32);initialized=False
        for t,x in enumerate(probe[n]):
            v=x.astype(np.int32)*256
            if not initialized:mean=v.copy();initialized=bool(np.any(x!=zero))
            d=v-mean;mean+=np.sign(d)*(np.abs(d)//32);d=v-mean;expected[n,t]=np.clip(np.sign(d)*((np.abs(d)+128)//256),-128,127)
    assert np.array_equal(expected,actual)
    counts={}
    for split in ('train','validation'):
        data=dict(np.load(a.previous/(split+'.npz'),allow_pickle=False));x=np.ascontiguousarray(data['x']);assert x.dtype==np.int8 and x.shape[2]==40
        assert not any(tag in str(s) for s in data['clip_id'] for tag in ('wide95-test-','wide95b-test-','wide95-validation-','wide95b-validation-'))
        out=np.empty_like(x);dll.transform(x.ctypes.data_as(pointer),out.ctypes.data_as(pointer),len(x),x.shape[1],zero.ctypes.data_as(pointer));data['x']=out
        np.savez_compressed(a.out/(split+'.npz'),**data);counts[split]=len(out);print(json.dumps({'split':split,'clips':len(out)}),flush=True)
    for name in ('normalization.json','channel.json'):(a.out/name).write_bytes((a.previous/name).read_bytes())
    meta=json.loads((a.previous/'features.json').read_text());meta.update(frontend='P4 causal integer log-Mel mean removal, s=1/32',contrast_header_sha256=sha(HEADER),contrast_script_sha256=sha(Path(__file__)),contrast_c_python_exact=True,contrast_zero_signature=zero.tolist(),contrast_previous_hashes={n:sha(a.previous/n) for n in ('train.npz','validation.npz','features.json','normalization.json')})
    (a.out/'features.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps({'complete':True,'counts':counts,'parity_frames':1200}),flush=True)
if __name__=='__main__':main()
