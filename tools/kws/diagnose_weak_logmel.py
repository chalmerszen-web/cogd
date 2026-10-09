"""Read-only TRAIN diagnosis of J versus F, fixed inputs and validation thresholds."""
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
import torch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from model import Model
from evaluate import metrics, events


def main():
    base=ROOT/'artifacts/kws-phase5'
    out=base/'adapt-j-diagnosis'
    out.mkdir(exist_ok=False)
    path=base/'features-weak-logmel/train.npz'
    data=np.load(path,allow_pickle=False)
    new=np.char.startswith(data['clip_id'],'weak-logmel-')
    chosen=np.flatnonzero(new | (data['device_domain']&(data['variant']==0)))
    selected={k:data[k][chosen] for k in data.files if k!='device_domain'}
    assert len(chosen)<=1000 and all(not str(g).endswith(('xiaoxiao','yunxia','bella','fenrir')) for g in selected['source_group'])
    (out/'selection.json').write_text(json.dumps(dict(indices=chosen.tolist(),clip_ids=selected['clip_id'].tolist(),
        features_sha256=hashlib.sha256(path.read_bytes()).hexdigest()),indent=2))
    torch.set_num_threads(4)
    reports={}
    for name in ('adapt-f','adapt-j'):
        checkpoint=base/name/'best.pt'
        payload=torch.load(checkpoint,weights_only=True,map_location='cpu')
        net=Model().eval();net.load_state_dict(payload['state_dict'])
        x=torch.from_numpy(selected['x'].astype(np.float32).transpose(0,2,1)/32)
        with torch.no_grad():
            scores=np.concatenate([net(x[i:i+32])[:,0].numpy()*256 for i in range(0,len(x),32)])
        frozen=json.loads((base/(name+'-int8/threshold.json')).read_text())['floating']
        summary={}
        for tag,mask in [('unattenuated',~new[chosen]),('weak6',new[chosen]&(selected['variant']==6)),
                         ('weak12',new[chosen]&(selected['variant']==12))]:
            local={k:v[mask] for k,v in selected.items()}
            metric=metrics(scores[mask],local,frozen)
            summary[tag]=dict(hits={k:v['hits'] for k,v in metric['languages'].items()},
                              totals={k:v['total'] for k,v in metric['languages'].items()},
                              negatives=metric['negative_triggered_clips'],negative_total=metric['negative_clips'],
                              early=metric['premature_events'])
        near=np.flatnonzero((selected['label']==0)&(selected['text']=='你好小燕'))
        near_events=[dict(clip_id=str(selected['clip_id'][i]),variant=int(selected['variant'][i]),
                          language=str(selected['language'][i]),events=events(scores[i],frozen)) for i in near]
        np.savez_compressed(out/(name+'-scores.npz'),scores=scores,clip_id=selected['clip_id'])
        reports[name]=dict(step=payload['step'],threshold_q8=frozen,summary=summary,near=near_events,
                           checkpoint_sha256=hashlib.sha256(checkpoint.read_bytes()).hexdigest())
    (out/'report.json').write_text(json.dumps(dict(complete=True,train_examples=len(chosen),models=reports,
        test_read=False,trained=False,flashed=False,limitation='TRAIN diagnostic, not acceptance or a capacity proof.'),indent=2))
    print(json.dumps({n:dict(step=r['step'],threshold=r['threshold_q8'],summary=r['summary']) for n,r in reports.items()}))


if __name__=='__main__':main()
