"""Frozen E scores + causal recent-energy diagnostic on captured validation.

No training, threshold adjustment, firmware change or test-source evaluation.
Warm-up energy median is a prototype noise reference, not a production VAD.
"""
import json
from pathlib import Path
import sys
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from data import Frontend,framed_example,normalize
from device_domain import pcm
from paired_negative import incomplete
from evaluate import events


def recent_energy(pcm,ratio=2.,hangover_blocks=6):
    energy=np.mean(np.asarray(pcm,dtype=float).reshape(-1,512)**2,axis=1)
    reference=max(1.,float(np.median(energy[:64])))
    active=energy>=reference*ratio
    return np.array([i>=63 and bool(np.any(active[max(0,i-hangover_blocks+1):i+1]))
                     for i in range(len(energy))]),reference


def gated_events(scores,threshold,allowed):
    votes=0;cooldown=0;result=[]
    for block,score in enumerate(scores[1::2],1):
        end=block*512;votes=((votes<<1)|int(score>=threshold))&7
        if block>=64 and end>=cooldown and votes.bit_count()>=2 and allowed[block-1]:
            result.append(end);cooldown=end+24000;votes=0
    return result


def main():
    base=ROOT/'artifacts/kws-phase5';out=base/'activity-pilot';out.mkdir(exist_ok=False)
    plan=dict(model='adapt-e',noise_reference='Median of first64 32ms blocks; causal at eligibility',
        energy_ratio=2.,hangover_blocks=6,threshold_changed=False,scope='captured validation only')
    (out/'plan.json').write_text(json.dumps(plan,indent=2))
    data=np.load(base/'features-paired/validation.npz');scores=np.load(base/'adapt-e-int8/validation-scores.npz')['quantized']
    threshold=json.loads((base/'adapt-e-int8/threshold.json').read_text())['quantized']
    stats=json.loads((base/'features-paired/normalization.json').read_text())
    captures=json.loads((base/'features/features.json').read_text(encoding='utf8'))['captures']
    captures={r['clip_id']:r for r in captures if r['split']=='validation'}
    parents={r['clip_id']:r for r in map(json.loads,(ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl').read_text(encoding='utf8').splitlines())}
    frontend=Frontend();results=[]
    for i in np.flatnonzero(data['device_domain']):
        clip=str(data['clip_id'][i]);kind=str(data['sampling_group'][i]);key=clip
        for suffix in ('-ambient','-prefix','-suffix'):
            if key.endswith(suffix):key=key[:-len(suffix)];break
        cap=captures[key];parent=parents[cap['parent_clip_id']];recorded=pcm(ROOT/cap['recording'])
        noise=recorded[-16000:];lo,hi=cap['crop'];source=recorded[lo:hi]
        row=dict(parent,label=int(data['label'][i]),speech_end_low_sample=cap['end_interval'][0],
            speech_end_high_sample=cap['end_interval'][1])
        if kind=='natural_negative':signal=np.resize(noise,65536).copy()
        else:
            if kind=='paired_negative':
                start=max(0,cap['lag_samples']-lo+parent['speech_start_sample'])
                source,_=incomplete(source,start,row['speech_end_low_sample'],clip.rsplit('-',1)[-1],ambient=noise)
            signal,_,_=framed_example(row,source)
            offset=(max(4096,33792-row['speech_end_low_sample'])+len(signal)-len(source)-1024)//2
            floor=np.resize(noise,len(signal));signal[:offset]=floor[:offset];signal[offset+len(source):]=floor[offset+len(source):]
        assert np.array_equal(normalize(frontend(signal),stats),data['x'][i]),clip
        allowed,reference=recent_energy(signal)
        before=events(scores[i],threshold);after=gated_events(scores[i],threshold,allowed)
        low=int(data['event_start_accept'][i]);high=int(data['event_end'][i])+12800
        results.append(dict(clip_id=clip,language=str(data['language'][i]),label=int(data['label'][i]),kind=kind,
            before=before,after=after,hit_before=any(low<=t<=high for t in before),
            hit_after=any(low<=t<=high for t in after),noise_rms=reference**.5))
    summary={lang:dict(total=sum(r['label'] and r['language']==lang for r in results),
        before=sum(r['label'] and r['language']==lang and r['hit_before'] for r in results),
        after=sum(r['label'] and r['language']==lang and r['hit_after'] for r in results)) for lang in ('zh','yue')}
    negative={kind:dict(total=sum(not r['label'] and r['kind']==kind for r in results),
        before=sum(not r['label'] and r['kind']==kind and bool(r['before']) for r in results),
        after=sum(not r['label'] and r['kind']==kind and bool(r['after']) for r in results))
        for kind in ('hard_negative','natural_negative','paired_negative')}
    report=dict(complete=True,feature_parity=len(results),languages=summary,negatives=negative,trials=results,
        deployable=False,limitation='Prototype fixed startup floor; no streaming noise adaptation, C parity or hardware gate yet.')
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(dict(languages=summary,negatives=negative,feature_parity=len(results))))


if __name__=='__main__':main()
