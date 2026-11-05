"""Bounded offline teacher experiment. Never loads the teacher on the P4.

Source/speaker splits remain those of the input manifest; physical recaptures
inherit their source split. Emit frame features and transcripts for diagnosis,
not a claim that ASR text equals wake accuracy.
"""
import argparse,json,sys,time,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
PROJECT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(PROJECT/'.local/sensevoice-python'))
import numpy as np
import torch
from funasr import AutoModel

def main():
    p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);torch.set_num_threads(4)
    began=time.monotonic()
    model=AutoModel(model='iic/SenseVoiceSmall',device='cuda:0',disable_update=True,hub='ms')
    config={'teacher':'iic/SenseVoiceSmall','device':'cuda:0','parameters':sum(p.numel() for p in model.model.parameters()),
            'loaded_seconds':time.monotonic()-began,'deployed_to_device':False}
    (a.out/'teacher.json').write_text(json.dumps(config,indent=2),encoding='utf8');print(config,flush=True)
    if not a.manifest:return
    rows=[json.loads(s) for s in a.manifest.read_text(encoding='utf8').splitlines() if s.strip()]
    features=[]
    def capture(module,args,output):
        x=output[0] if isinstance(output,tuple) else output
        features.append(x.detach().float().cpu().numpy())
    hook=model.model.encoder.register_forward_hook(capture)
    results=[]
    try:
        for i,r in enumerate(rows):
            if time.monotonic()-began>3600:raise TimeoutError('One-hour teacher extraction limit')
            path=ROOT/r['path'];features.clear()
            result=model.generate(input=str(path),cache={},language='auto',use_itn=False,batch_size_s=60)
            filename=f'{i:04d}.npz';np.savez_compressed(a.out/filename,frames=features[-1][0])
            row=dict(r,teacher=result,features=filename,wav_sha256=hashlib.sha256(path.read_bytes()).hexdigest());results.append(row)
            (a.out/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8')
            print(json.dumps({'n':i+1,'total':len(rows),'id':r.get('clip_id'),'text':result},ensure_ascii=False),flush=True)
    finally:hook.remove()

if __name__=='__main__':main()
