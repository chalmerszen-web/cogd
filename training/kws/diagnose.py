"""Explain frozen test events without changing scores, thresholds or acceptance."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import numpy as np
from evaluate import events


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--bundle',type=Path,required=True)
    parser.add_argument('--features',type=Path,required=True)
    parser.add_argument('--manifest',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    paths=[args.bundle/'test-scores.npz',args.bundle/'test-metrics.json',
           args.bundle/'threshold.json',args.features/'test.npz',args.manifest]
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    metric=json.loads(paths[1].read_text())
    threshold=json.loads(paths[2].read_text())['quantized']
    assert metric['quantized']['threshold_q8']==threshold
    assert metric['evaluated_features_sha256']==sha(paths[3])
    data=np.load(paths[3],allow_pickle=False)
    scores=np.load(paths[0],allow_pickle=False)
    np.testing.assert_array_equal(data['clip_id'],scores['clip_id'])
    sources={r['clip_id']:r for r in map(json.loads,args.manifest.read_text(encoding='utf8').splitlines())}
    groups={};details=[];negative=[]
    for i,clip_id in enumerate(data['clip_id']):
        row=sources[str(clip_id)];times=events(scores['quantized'][i],threshold)
        if not data['label'][i]:
            if times:negative.append(dict(clip_id=str(clip_id),language=row['language'],
                text=row.get('text'),source_text=row.get('source_text'),events_samples=times))
            continue
        end=int(data['event_end'][i]);delta=[(t-end)/16 for t in times]
        hit=any(0<=t<=800 for t in delta)
        kind='hit' if hit else 'early' if any(t<0 for t in delta) else 'late' if delta else 'no_event'
        group=groups.setdefault(str(data['language'][i]),Counter())
        group[kind]+=1
        if not hit:details.append(dict(clip_id=str(clip_id),language=row['language'],
            source_group=row['source_group'],kind=kind,event_offsets_ms=delta))
    for lang,counts in groups.items():
        assert counts['hit']==metric['quantized']['languages'][lang]['hits']
        assert sum(counts.values())==metric['quantized']['languages'][lang]['total']
    assert len(negative)==metric['quantized']['negative_triggered_clips']
    early={}
    for lang in groups:
        values=[t for d in details if d['language']==lang for t in d['event_offsets_ms'] if t<0]
        early[lang]=dict(count=len(values),minimum_ms=min(values) if values else None,
            median_ms=float(np.median(values)) if values else None,maximum_ms=max(values) if values else None,
            within_32ms=sum(t>=-32 for t in values),within_64ms=sum(t>=-64 for t in values))
    output=dict(scope='Post-test diagnosis only. No rescore, threshold change or new training.',
        input_sha256={str(p):sha(p) for p in paths},groups=groups,early_offsets=early,
        positive_failures=details,negative_triggers=negative,
        boundary_caveat='Word ends are automatic approximate annotations, not human phonetic labels.')
    with args.out.open('x',encoding='utf8') as stream:
        json.dump(output,stream,indent=2,ensure_ascii=False);stream.write('\n')
    print(json.dumps(dict(groups=groups,early_offsets=early,negative_triggers=negative),indent=2,ensure_ascii=False))


if __name__=='__main__':main()
