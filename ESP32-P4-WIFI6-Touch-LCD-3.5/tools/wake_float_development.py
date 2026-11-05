"""Compare float and exported integer decisions on DEVELOPMENT microphone PCM."""
import argparse,ctypes as C,importlib.util,json,sys
from pathlib import Path
import numpy as np,torch
from wake_prepare import ROOT,pcm
sys.path[:0]=[str(Path(__file__).resolve().parent/'kws96_training'),str(ROOT/'training/kws')]
spec=importlib.util.spec_from_file_location('p4_float_model96',Path(__file__).resolve().parent/'kws96_training/model.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);Model=module.Model
from data import Frontend,normalize
from wake_host import Oracle,build
def main():
    p=argparse.ArgumentParser();p.add_argument('--models',type=Path,required=True);p.add_argument('--capture',type=Path,required=True);p.add_argument('--curve',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(4);capture=json.loads(a.capture.read_text(encoding='utf8'));curve=json.loads((a.curve/'report.json').read_text());assert all(t['source']['split']=='validation' for t in capture['trials'])
    models=[];norm=None
    for name in ('first','second','verifier'):
        saved=torch.load(a.models/name/'best.pt',map_location='cpu',weights_only=True);m=Model(saved['config']['channels']).eval();m.load_state_dict(saved['state_dict']);models.append(m);norm=saved['normalization']
    front=Frontend();x=np.stack([normalize(front(np.pad(pcm(t['path']),(32768,0))),norm) for t in capture['trials']]);tensor=torch.from_numpy(x.astype(np.float32).transpose(0,2,1)/32)
    with torch.no_grad():heads=np.stack([m(tensor)[:,0].numpy() for m in models],axis=-1)
    heads=np.clip(np.sign(heads)*np.floor(abs(heads)*256+.5),-32768,32767).astype(np.int32)
    block=heads[:,1::2];pair=np.stack([np.trunc((block[:,:,0]+block[:,:,1])/2),block[:,:,2]],axis=-1).astype(np.int32)
    averaged=np.zeros_like(pair)
    for i in range(pair.shape[1]):averaged[:,i]=np.trunc(pair[:,max(0,i-2):i+1].mean(1))
    oracle=Oracle(build(a.models,a.out/'host'));dll=oracle.lib;dll.decision_reset.argtypes=[C.c_int];dll.decision_step.argtypes=[C.c_int,C.c_int,C.c_uint64]
    report={'scope':'DEVELOPMENT diagnosis, not physical acceptance','thresholds':{},'deltas':[]}
    for i,(trial,meta) in enumerate(zip(capture['trials'],curve['trials'])):
        name=trial['source']['clip_id'];integer=np.load(a.curve/(name+'.npz'))['scores'];floating=averaged[i,64:]
        delta=floating-integer[:,3:];report['deltas'].append({'id':name,'mean_abs_q8':np.abs(delta).mean(0).tolist(),'max_abs_q8':np.abs(delta).max(0).tolist()});np.savez_compressed(a.out/(name+'.npz'),floating=floating,integer=integer)
    for setting in (500,600,740,820,900):
        threshold=int(np.floor(np.log(setting/(1000-setting))*256+.5));groups={}
        for i,meta in enumerate(curve['trials']):
            dll.decision_reset(threshold);events=[(k+1)*512 for k,s in enumerate(averaged[i,64:]) if dll.decision_step(int(s[0]),int(s[1]),32768+(k+1)*512)]
            lo,hi=meta['accept_bounds'];hit=bool(meta['label']) and any(lo<=e<=hi for e in events);false=any(not meta['label'] or not lo<=e<=hi for e in events);g=groups.setdefault(meta['group'],{'count':0,'hits':0,'false':0});g['count']+=1;g['hits']+=hit;g['false']+=false
        report['thresholds'][str(setting)]=groups;print(json.dumps({'setting':setting,'groups':groups}),flush=True)
    report['complete']=True;(a.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
if __name__=='__main__':main()
