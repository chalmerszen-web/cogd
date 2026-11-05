"""Screen the32ms large model on DEVELOPMENT PCM and the fixed C policy."""
import argparse,ctypes as C,json,sys
from pathlib import Path
import numpy as np
import torch
from wake_word256 import Model,CONTRACT,score
from wake_prepare import ROOT,pcm
from wake_host import build,Oracle
sys.path.insert(0,str(ROOT/'training/kws'))
from data import Frontend,normalize

def main():
    p=argparse.ArgumentParser();p.add_argument('--model',type=Path,required=True);p.add_argument('--policy-models',type=Path,required=True)
    p.add_argument('--capture',type=Path,required=True);p.add_argument('--curve',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(4);saved=torch.load(a.model,map_location='cpu',weights_only=True);assert saved['config']['contract']==CONTRACT
    model=Model().eval();model.load_state_dict(saved['state_dict']);capture=json.loads(a.capture.read_text(encoding='utf8'));curve=json.loads((a.curve/'report.json').read_text())
    assert capture['complete'] and all(t['source']['split']=='validation' for t in capture['trials'])
    front=Frontend();values=np.stack([normalize(front(np.pad(pcm(t['path']),(32768,0))),saved['normalization']) for t in capture['trials']])
    tensor=torch.from_numpy(values.astype(np.float32).transpose(0,2,1)/32)
    with torch.no_grad():outputs=model(tensor);raw=score(outputs)[:,0,1::2].numpy();classes=outputs[:,:,1::2].argmax(1).numpy()
    q=np.clip(np.sign(raw)*np.floor(abs(raw)*256+.5),-32768,32767).astype(np.int32)
    averaged=np.stack([np.trunc(q[:,max(0,k-2):k+1].mean(1)) for k in range(q.shape[1])],axis=1).astype(np.int32)[:,64:]
    oracle=Oracle(build(a.policy_models,a.out/'policy'));dll=oracle.lib;dll.decision_reset.argtypes=[C.c_int];dll.decision_step.argtypes=[C.c_int,C.c_int,C.c_uint64]
    report={'scope':'Float five-class DEVELOPMENT screening; not integer or physical acceptance','thresholds':{},'trials':curve['trials']}
    for i,trial in enumerate(curve['trials']):np.savez_compressed(a.out/(trial['id']+'.npz'),score=averaged[i],classes=classes[i,64:])
    for setting in range(500,951,10):
        threshold=int(np.floor(np.log(setting/(1000-setting))*256+.5));groups={};rows=[]
        for i,meta in enumerate(curve['trials']):
            dll.decision_reset(threshold);events=[(k+1)*512 for k,s in enumerate(averaged[i]) if dll.decision_step(int(s),int(s),32768+(k+1)*512)]
            lo,hi=meta['accept_bounds'];hit=bool(meta['label']) and any(lo<=e<=hi for e in events);false=any(not meta['label'] or not lo<=e<=hi for e in events)
            g=groups.setdefault(meta['group'],{'count':0,'hits':0,'false':0});g['count']+=1;g['hits']+=hit;g['false']+=false;rows.append({'id':meta['id'],'events':events,'hit':hit,'false':false})
        passed=all(g['false']==0 and (name=='negative' or g['hits']/g['count']>=.95) for name,g in groups.items())
        report['thresholds'][str(setting)]={'groups':groups,'passed':passed,'rows':rows}
        if setting in (500,600,740,820,900):print(json.dumps({'setting':setting,'groups':groups}),flush=True)
    report['passing_settings']=[s for s,r in report['thresholds'].items() if r['passed']];report['complete']=True
    (a.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8');print(json.dumps({'passing_settings':report['passing_settings']}))

if __name__=='__main__':main()
