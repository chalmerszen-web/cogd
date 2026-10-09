"""Once-only weak development diagnosis at a frozen two-member threshold."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from data import Frontend,framed_example,normalize
from append_captures import capture_path
from device_domain import pcm
from paired_negative import incomplete
from evaluate import metrics,scores_for
from test_frozen_fusion import combine


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--secondary',choices=('adapt-i','adapt-j','adapt-k'),default='adapt-i')
    args=parser.parse_args()
    members_names=('adapt-e',args.secondary)
    base=ROOT/'artifacts/kws-phase5';out=base/('fusion-e'+args.secondary[-1]+'-smooth3')
    assert not (out/'weak-validation.json').exists()
    frozen=json.loads((out/'threshold.json').read_text())
    original=json.loads((out/'validation-metrics.json').read_text())
    data=np.load(base/'features-paired/validation.npz',allow_pickle=False)
    indices=np.flatnonzero(data['device_domain']);assert len(indices)==88
    subset={k:data[k][indices] for k in data.files if k!='device_domain'}
    stats=json.loads((base/'features-paired/normalization.json').read_text())
    captures={r['clip_id']:r for r in json.loads((base/'features/features.json').read_text(encoding='utf8'))['captures'] if r['split']=='validation'}
    parents={r['clip_id']:r for r in map(json.loads,(ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl').read_text(encoding='utf8').splitlines())}
    frontend=Frontend();features={6:[],12:[]}
    for i in indices:
        clip=str(data['clip_id'][i]);kind=str(data['sampling_group'][i]);key=clip
        for suffix in ('-ambient','-prefix','-suffix'):
            if key.endswith(suffix):key=key[:-len(suffix)];break
        cap=captures[key];parent=parents[cap['parent_clip_id']]
        assert cap['split']==parent['split']=='validation'
        recorded=pcm(capture_path(cap['recording']));noise=recorded[-16000:]
        lo,hi=cap['crop'];source=recorded[lo:hi]
        row=dict(parent,label=int(data['label'][i]),speech_end_low_sample=cap['end_interval'][0],speech_end_high_sample=cap['end_interval'][1])
        if kind=='natural_negative':signal=np.resize(noise,65536).copy()
        else:
            if kind=='paired_negative':
                start=max(0,cap['lag_samples']-lo+parent['speech_start_sample'])
                source,_=incomplete(source,start,row['speech_end_low_sample'],clip.rsplit('-',1)[-1],ambient=noise)
            signal,_,_=framed_example(row,source)
            offset=(max(4096,33792-row['speech_end_low_sample'])+len(signal)-len(source)-1024)//2
            floor=np.resize(noise,len(signal));signal[:offset]=floor[:offset];signal[offset+len(source):]=floor[offset+len(source):]
        np.testing.assert_array_equal(normalize(frontend(signal),stats),data['x'][i])
        ambient=np.resize(np.roll(noise,7919),len(signal)).astype(float)
        for db in (6,12):
            scale=10**(-db/20)
            mixed=np.clip(np.rint(signal.astype(float)*scale+ambient*np.sqrt(1-scale*scale)),-32768,32767).astype(np.int16)
            features[db].append(normalize(frontend(mixed),stats))
    bundles={name:json.loads((base/(name+'-int8/model.json')).read_text()) for name in members_names}
    for name,bundle in bundles.items():
        assert hashlib.sha256((base/name/'best.pt').read_bytes()).hexdigest()==bundle['checkpoint_sha256']
    results={}
    for db in (6,12):
        local=dict(subset,x=np.stack(features[db]))
        members=[scores_for(local,bundles[name],base/name/'best.pt') for name in members_names]
        results[str(db)]={}
        saved={}
        for column,kind in enumerate(('quantized','floating')):
            scores=combine(members[0][column],members[1][column],integer=kind=='quantized')
            results[str(db)][kind]=metrics(scores,local,frozen[kind]);saved[kind]=scores
        np.savez_compressed(out/f'weak{db}-scores.npz',**saved,clip_id=subset['clip_id'])
    baseline=json.loads((base/'noise-frontend-pilot/report.json').read_text())['results']
    gates=dict(original=all(original['host_gate'].values()),
        weak_nonregression=all(results[str(db)]['quantized']['languages'][lang]['hits']>=baseline[str(db)]['baseline']['languages'][lang]['hits'] for db in (6,12) for lang in ('zh','yue')),
        weak_negatives=all(results[str(db)]['quantized']['negative_triggered_clips']==0 for db in (6,12)),
        yue_improves=sum(results[str(db)]['quantized']['languages']['yue']['hits']-baseline[str(db)]['baseline']['languages']['yue']['hits'] for db in (6,12))>=2)
    with (out/'weak-validation.json').open('x') as f:
        json.dump(dict(complete=True,threshold=frozen,results=results,gates=gates,advance=all(gates.values()),
            exact_original_reconstructions=88,source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            model_hashes={n:hashlib.sha256((base/(n+'-int8/model.json')).read_bytes()).hexdigest() for n in bundles},
            limitation='Frozen failed-candidate diagnosis on approximate reused validation mixtures; not physical metres, new blind data or release.'),f,indent=2)
    print(json.dumps(dict(gates=gates,weak={db:dict(hits={k:v['hits'] for k,v in r['quantized']['languages'].items()},false=r['quantized']['negative_triggered_clips']) for db,r in results.items()})))


if __name__=='__main__':main()
