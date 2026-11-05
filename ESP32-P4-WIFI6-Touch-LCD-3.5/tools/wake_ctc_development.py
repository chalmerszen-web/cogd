"""Float phonetic screening on frozen DEVELOPMENT captures, never final."""
import argparse,json,sys
from pathlib import Path
import numpy as np
import torch
from wake_ctc96 import Model,CONTRACT,decode,collapse,TOKENS
from wake_prepare import ROOT,pcm
sys.path.insert(0,str(ROOT/'training/kws'))
from data import Frontend,normalize

def main():
    p=argparse.ArgumentParser();p.add_argument('--model',type=Path,required=True);p.add_argument('--capture',type=Path,required=True)
    p.add_argument('--curve',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(4);saved=torch.load(a.model,map_location='cpu',weights_only=True);assert saved['config']['contract']==CONTRACT
    model=Model().eval();model.load_state_dict(saved['state_dict']);capture=json.loads(a.capture.read_text(encoding='utf8'));curve=json.loads((a.curve/'report.json').read_text())
    assert capture['complete'] and all(t['source']['split']=='validation' for t in capture['trials'])
    front=Frontend();values=np.stack([normalize(front(np.pad(pcm(t['path']),(32768,0))),saved['normalization']) for t in capture['trials']])
    tensor=torch.from_numpy(values.astype(np.float32).transpose(0,2,1)/32)
    with torch.no_grad():logits=model(tensor)[:,:,1::2].transpose(1,2).numpy()
    report={'scope':'Float CTC DEVELOPMENT screening; not hardware/integer acceptance','groups':{},'trials':[]}
    for trial,meta,row in zip(capture['trials'],curve['trials'],logits):
        assert meta['id']==trial['source']['clip_id'];events=decode(row[64:]);lo,hi=meta['accept_bounds'];positive=bool(meta['label'])
        valid=lambda event:positive and lo<=event[0]<=hi and event[1]==meta['group']
        hit=any(valid(e) for e in events);false=any(not valid(e) for e in events)
        g=report['groups'].setdefault(meta['group'],{'count':0,'hits':0,'false':0});g['count']+=1;g['hits']+=hit;g['false']+=false
        report['trials'].append({'id':meta['id'],'hit':hit,'false':false,'events':events,'accept_bounds':[lo,hi],'transcript':[TOKENS[i] for i in collapse(row[64:])]})
        np.savez_compressed(a.out/(meta['id']+'.npz'),logits=row[64:])
    report['complete']=True;(a.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8');print(json.dumps(report['groups']),flush=True)

if __name__=='__main__':main()
