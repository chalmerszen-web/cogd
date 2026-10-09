"""Freeze threshold on validation; evaluate independent test only afterwards."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
import torch
from model import Model
from quantize import infer

def events(scores,threshold):
    votes=0;cooldown=0;result=[]
    for block,score in enumerate(scores[1::2],1):
        end=block*512;votes=((votes<<1)|int(score>=threshold))&7
        if block>=64 and end>=cooldown and votes.bit_count()>=2:
            result.append(end);cooldown=end+24000;votes=0
    return result

def threshold_q8(per_mille):
    # Match the existing device's float probability -> lroundf(logit * 256).
    probability=np.float32(per_mille)/np.float32(1000)
    logit=np.log(probability/(np.float32(1)-probability))*np.float32(256)
    return int(np.floor(logit+.5))

def metrics(scores,data,threshold):
    report={'threshold_q8':int(threshold),'languages':{},'negative_clips':0,'negative_events':0,'negative_triggered_clips':0,'premature_events':0}
    for language in ('zh','yue'):
        hits=0;latencies=[];count=0;groups={}
        for index in np.flatnonzero((data['label']==1)&(data['language']==language)):
            count+=1;times=events(scores[index],threshold);end=int(data['event_end'][index])
            low=int(data['event_start_accept'][index]) if 'event_start_accept' in data else end
            valid=[t for t in times if low<=t<=end+12800]
            report['premature_events']+=sum(t<low for t in times)
            if valid:hits+=1;latencies.append((valid[0]-end)/16)
            if 'source_group' in data:
                group=groups.setdefault(str(data['source_group'][index]),dict(total=0,hits=0,early_clips=0))
                group['total']+=1;group['hits']+=bool(valid);group['early_clips']+=any(t<low for t in times)
        for group in groups.values():group['recall']=group['hits']/group['total']
        report['languages'][language]=dict(total=count,hits=hits,misses=count-hits,recall=hits/count if count else None,
            source_groups=groups,
            latency_ms_p50=float(np.median(latencies)) if latencies else None,
            latency_ms_p95=float(np.percentile(latencies,95)) if latencies else None,
            latency_ms_max=max(latencies) if latencies else None)
    for index in np.flatnonzero(data['label']==0):
        detected=events(scores[index],threshold)
        report['negative_clips']+=1;report['negative_events']+=len(detected)
        report['negative_triggered_clips']+=bool(detected)
    if 'event_start_accept' in data:
        report['acceptance']='Frozen endpoint uncertainty lower bound through upper bound +800ms; latency relative to upper bound'
    if 'sampling_group' in data:
        report['negative_groups']={}
        report['partial_word_triggers']=0;report['partial_word_total']=0
        for index in np.flatnonzero(data['label']==0):
            key=str(data['sampling_group'][index]);g=report['negative_groups'].setdefault(key,dict(total=0,triggered=0))
            triggered=bool(events(scores[index],threshold));g['total']+=1;g['triggered']+=triggered
            if 'text' in data and str(data['text'][index]) in ('你好','小言'):
                report['partial_word_total']+=1;report['partial_word_triggers']+=triggered
    if 'device_domain' in data and np.any(data['device_domain']):
        device=np.flatnonzero(data['device_domain'])
        subset={key:data[key][device] for key in data.keys() if key!='device_domain'}
        report['device']=metrics(scores[device],subset,threshold)
    return report

def scores_for(data,bundle,checkpoint):
    quantized=np.stack([infer(x,bundle)[:,-1] for x in data['x']])
    net=Model().eval();net.load_state_dict(torch.load(checkpoint,map_location='cpu',weights_only=True)['state_dict'])
    pieces=[]
    with torch.no_grad():
        for start in range(0,len(data['x']),32):
            x=torch.from_numpy(data['x'][start:start+32].astype(np.float32).transpose(0,2,1)/32)
            pieces.append(net(x)[:,0,:].numpy()*256)
    return quantized,np.concatenate(pieces)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--features',type=Path,required=True)
    ap.add_argument('--bundle',type=Path,required=True);ap.add_argument('--checkpoint',type=Path,required=True)
    ap.add_argument('--split',choices=['validation','test'],default='validation');args=ap.parse_args()
    torch.set_num_threads(4)
    bundle=json.loads((args.bundle/'model.json').read_text());data=np.load(args.features/(args.split+'.npz'),allow_pickle=False)
    for name,expected in bundle.get('feature_hashes',{}).items():
        if hashlib.sha256((args.features/name).read_bytes()).hexdigest()!=expected:
            raise ValueError('Frozen feature input changed: '+name)
    if hashlib.sha256(args.checkpoint.read_bytes()).hexdigest()!=bundle['checkpoint_sha256']:
        raise ValueError('Float checkpoint does not match quantized bundle')
    if (args.bundle/(args.split+'-metrics.json')).exists():
        raise ValueError('Preserve completed evaluation; use a separate diagnosed experiment')
    quantized,floating=scores_for(data,bundle,args.checkpoint)
    frozen=args.bundle/'threshold.json'
    if args.split=='validation':
        if frozen.exists():raise ValueError('Threshold already frozen; preserve prior evidence')
        def select(scores):
            candidates=[]
            for probability in range(500,951,10):
                metric=metrics(scores,data,threshold_q8(probability))
                metric['threshold_per_mille']=probability;candidates.append(metric)
            # Penalize negative/premature triggers; prioritize the weaker language.
            def rank(m):
                worst=min(v['recall'] for v in m['languages'].values())
                penalty=(m['negative_events']+m['premature_events'])/max(1,len(data['x']))
                if 'device' in m:
                    d=m['device'];worst=min(worst,min(v['recall'] for v in d['languages'].values()))
                    penalty=max(penalty,(d['negative_events']+d['premature_events'])/max(1,int(data['device_domain'].sum())))
                return worst-4*penalty,-m['negative_events'],-m['premature_events'],m['threshold_q8']
            return max(candidates,key=rank)
        q=select(quantized);f=select(floating)
        frozen.write_text(json.dumps(dict(quantized=q['threshold_q8'],floating=f['threshold_q8'],
            device_threshold_per_mille=q['threshold_per_mille'],selection_split='validation'),indent=2)+'\n')
    else:
        threshold=json.loads(frozen.read_text());q=metrics(quantized,data,threshold['quantized']);f=metrics(floating,data,threshold['floating'])
    report=dict(split=args.split,quantized=q,floating=f,
        checkpoint_sha256=bundle['checkpoint_sha256'],
        model_json_sha256=hashlib.sha256((args.bundle/'model.json').read_bytes()).hexdigest(),
        evaluated_features_sha256=hashlib.sha256((args.features/(args.split+'.npz')).read_bytes()).hexdigest(),
        recall_drop={language:f['languages'][language]['recall']-q['languages'][language]['recall'] for language in ('zh','yue')},
        human_generalization_verified=False)
    (args.bundle/(args.split+'-metrics.json')).write_text(json.dumps(report,indent=2)+'\n')
    np.savez_compressed(args.bundle/(args.split+'-scores.npz'),quantized=quantized,floating=floating,clip_id=data['clip_id'])
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
