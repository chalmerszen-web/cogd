"""Two predeclared frozen-encoder classifiers; no development threshold tuning."""
import argparse,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('directory',type=Path);a=p.parse_args()
rows=json.loads((a.directory/'results.json').read_text(encoding='utf8'))
train=np.array([r['split']=='train' for r in rows]);y=np.array([r['label'] for r in rows])
assert not {r['voice'] for r,t in zip(rows,train) if t}&{r['voice'] for r,t in zip(rows,train) if not t}
all_features=[np.load(a.directory/r['features'])['frames'][4:] for r in rows]
report={'scope':'speaker-disjoint software development; not hardware wake acceptance','models':[]}
for bins in (1,4):
 x=np.array([np.concatenate([v for b in np.array_split(f,bins) for v in (b.mean(0),b.std(0))]) for f in all_features])
 mean=x[train].mean(0);scale=np.maximum(x[train].std(0),.1);x=(x-mean)/scale
 x/=np.maximum(np.linalg.norm(x,axis=1,keepdims=True),1e-6)
 xx=x[train];target=y[train]*2.-1.;kernel=xx@xx.T
 inverse=np.linalg.inv(kernel+.1*np.eye(len(xx)));alpha=inverse@target
 # LOO predictions choose a training-negative gate, without looking at DEV.
 loo=target-alpha/np.diag(inverse);threshold=max(0.,float(loo[y[train]==0].max())+.05)
 scores=x@xx.T@alpha;pred=scores>=threshold
 groups={}
 for g in sorted({r['wake_group'] for r in rows}):
  ids=np.array([r['wake_group']==g for r in rows])&~train
  groups[g]={'count':int(ids.sum()),'accepted':int(pred[ids].sum())}
 config={'bins':bins,'regularization':.1,'threshold':threshold,'groups':groups,'scores':[float(s) for s in scores],
         'teacher_parameters':233999167,'deployable_on_p4':False}
 report['models'].append(config)
 np.savez_compressed(a.directory/f'head-{bins}.npz',mean=mean,scale=scale,train=xx,alpha=alpha,threshold=threshold)
print(json.dumps([{k:v for k,v in r.items() if k!='scores'} for r in report['models']],ensure_ascii=False,indent=2))
(a.directory/'classifier-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
