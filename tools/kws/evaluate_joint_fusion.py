"""One fixed equal-logit fusion on validation; no weight search or test access."""
import hashlib
import argparse
import json
from pathlib import Path
import sys
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from evaluate import metrics,threshold_q8


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--smoothing-blocks',type=int,choices=(1,3),default=1)
    parser.add_argument('--secondary',choices=('adapt-f','adapt-i','adapt-j','adapt-k'),default='adapt-f')
    args=parser.parse_args()
    base=ROOT/'artifacts/kws-phase5'
    pair='e'+args.secondary[-1]
    out=base/('fusion-'+pair+('' if args.smoothing_blocks==1 else '-smooth3'));out.mkdir(exist_ok=False)
    data=np.load(base/'features-paired/validation.npz')
    sources={};sums={}
    for candidate in ('adapt-e',args.secondary):
        path=base/(candidate+'-int8');saved=np.load(path/'validation-scores.npz')
        assert np.array_equal(saved['clip_id'],data['clip_id'])
        report=json.loads((path/'validation-metrics.json').read_text())
        assert report['evaluated_features_sha256']==hashlib.sha256((base/'features-paired/validation.npz').read_bytes()).hexdigest()
        sources[candidate]={name:hashlib.sha256((path/name).read_bytes()).hexdigest() for name in ('model.json','model.c','validation-scores.npz')}
        for kind in ('quantized','floating'):sums[kind]=sums.get(kind,0)+saved[kind].astype(np.float64)
    plan=dict(recipe='equal mean of '+pair.upper()+' logits; int sum division rounds toward zero',
        coefficients=[1,1],weight_search=False,sources=sources,selection_split='validation',smoothing_blocks=args.smoothing_blocks,
        estimated_two_workspace_bytes=18896,actual_device_resources_verified=False)
    (out/'plan.json').write_text(json.dumps(plan,indent=2))
    scores={kind:np.trunc(values/2).astype(np.int16) if kind=='quantized' else values/2 for kind,values in sums.items()}
    if args.smoothing_blocks==3:
        for kind,values in scores.items():
            original=values[:,1::2].astype(float)
            for i in range(original.shape[1]):
                average=original[:,max(0,i-2):i+1].mean(1)
                values[:,2*i+1]=np.trunc(average) if kind=='quantized' else average
    report={}
    for kind,values in scores.items():
        candidates=[]
        for probability in range(500,951,10):
            m=metrics(values,data,threshold_q8(probability));m['threshold_per_mille']=probability;candidates.append(m)
        def rank(m):
            worst=min(v['recall'] for v in m['languages'].values());d=m['device']
            worst=min(worst,min(v['recall'] for v in d['languages'].values()))
            penalty=max((m['negative_events']+m['premature_events'])/len(data['x']),
                (d['negative_events']+d['premature_events'])/int(data['device_domain'].sum()))
            return worst-4*penalty,-m['negative_events'],-m['premature_events'],m['threshold_q8']
        report[kind]=max(candidates,key=rank)
    q=report['quantized'];d=q['device'];n=d['negative_groups']
    report['host_gate']=dict(zh=d['languages']['zh']['hits']==8,yue=d['languages']['yue']['hits']>=(7 if pair=='ef' else 8),
        no_recorded_negatives=d['negative_triggered_clips']==0,no_partial=q['partial_word_triggers']==0,
        overall_recall=all(v['recall']>=.9 for v in q['languages'].values()),
        negative_rate=q['negative_triggered_clips']/q['negative_clips']<=.05)
    if pair!='ef':
        from evaluate import events
        near=np.flatnonzero((data['label']==0)&(data['text']=='你好小燕'))
        assert len(near)>=3
        report['host_gate']['no_near_word']=not any(events(scores['quantized'][i],q['threshold_q8']) for i in near)
    report['ready_for_c_prototype']=all(report['host_gate'].values());report['five_metres_verified']=False
    (out/'validation-metrics.json').write_text(json.dumps(report,indent=2))
    (out/'threshold.json').write_text(json.dumps(dict(quantized=q['threshold_q8'],
        floating=report['floating']['threshold_q8'],device_threshold_per_mille=q['threshold_per_mille'],selection_split='validation'),indent=2))
    np.savez_compressed(out/'validation-scores.npz',**scores,clip_id=data['clip_id'])
    print(json.dumps(dict(threshold=q['threshold_per_mille'],languages=d['languages'],negatives=n,gates=report['host_gate'])))


if __name__=='__main__':main()
