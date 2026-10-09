"""Compare frozen C validation to identical original B validation examples."""
import hashlib
import argparse
import json
from pathlib import Path
import sys
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from evaluate import metrics


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--candidate',choices=('adapt-c','adapt-d','adapt-e','adapt-f'),default='adapt-c')
    args=parser.parse_args();candidate=args.candidate
    out=ROOT/'artifacts/kws-phase5';base=np.load(out/'features-channel/validation.npz')
    data=np.load(out/'features-paired/validation.npz');n=len(base['x'])
    for key in base.files:assert np.array_equal(base[key],data[key][:n]),key
    threshold=json.loads((out/(candidate+'-int8')/'threshold.json').read_text())
    scores=np.load(out/(candidate+'-int8')/'validation-scores.npz')
    assert np.array_equal(scores['clip_id'],data['clip_id'])
    original=metrics(scores['quantized'][:n],base,threshold['quantized'])
    paired=metrics(scores['quantized'][n:],{k:data[k][n:] for k in data.files},threshold['quantized'])
    prior=json.loads((out/'adapt-b-int8/validation-metrics.json').read_text())['quantized']
    gates=dict(device_recall_not_lower=all(original['device']['languages'][lang]['hits']>=prior['device']['languages'][lang]['hits'] for lang in ('zh','yue')),
        no_device_paired_triggers=paired['device']['negative_triggered_clips']==0,
        no_standalone_partial_triggers=original['partial_word_triggers']==0,
        no_device_negative_triggers=original['device']['negative_triggered_clips']==0,
        original_recall=all(v['recall']>=.9 for v in original['languages'].values()),
        original_negative_rate=original['negative_triggered_clips']/original['negative_clips']<=.05)
    result=dict(candidate=candidate,original=original,paired=paired,baseline=prior,gates=gates,
        ready_for_device_experiment=all(gates.values()),five_metre_verified=False,
        model_sha256=hashlib.sha256((out/(candidate+'-int8')/'model.json').read_bytes()).hexdigest())
    with (out/(candidate+'-comparison.json')).open('x',encoding='utf8') as f:json.dump(result,f,indent=2)
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
