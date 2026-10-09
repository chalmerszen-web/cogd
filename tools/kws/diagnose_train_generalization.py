"""Finite float diagnostic on TRAIN examples; no optimization or test access."""
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
import torch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from model import Model
from evaluate import metrics


def main():
    out=ROOT/'artifacts/kws-phase5';features=out/'features-paired/train.npz'
    data=np.load(features);indices=[]
    for domain in (False,True):
        for language in ('zh','yue'):
            for kind in ('positive','hard_negative','paired_negative','natural_negative'):
                members=np.flatnonzero((data['variant']==0)&(data['device_domain']==domain)&
                    (data['language']==language)&(data['sampling_group']==kind))
                # Stable hash order is unrelated to scores or recognition outcomes.
                members=sorted(members,key=lambda i:hashlib.sha256(str(data['clip_id'][i]).encode()).hexdigest())
                indices.extend(members[:12])
    indices=np.array(indices);subset={k:data[k][indices] for k in data.files}
    with (out/'train-generalization-selection.json').open('x',encoding='utf8') as f:
        json.dump(dict(features_sha256=hashlib.sha256(features.read_bytes()).hexdigest(),
            indices=indices.tolist(),clip_ids=subset['clip_id'].tolist()),f,indent=2)
    torch.set_num_threads(4);results={}
    for candidate in ('adapt-b','adapt-c','adapt-d'):
        net=Model().eval();net.load_state_dict(torch.load(out/candidate/'best.pt',weights_only=True,map_location='cpu')['state_dict'])
        threshold=json.loads((out/(candidate+'-int8')/'threshold.json').read_text())['floating']
        with torch.no_grad():
            x=torch.from_numpy(subset['x'].astype(np.float32).transpose(0,2,1)/32)
            scores=net(x)[:,0,:].numpy()*256
        results[candidate]=metrics(scores,subset,threshold)
        np.savez_compressed(out/(candidate+'-train-diagnosis-scores.npz'),scores=scores,indices=indices)
    with (out/'train-generalization-diagnosis.json').open('x',encoding='utf8') as f:
        json.dump(dict(complete=True,train_examples=len(indices),results=results,
            limitation='In-sample float diagnostic at independently frozen validation thresholds, not an acceptance test.'),f,indent=2)
    print(json.dumps(results,indent=2))


if __name__=='__main__':main()
