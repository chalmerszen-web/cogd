"""Train-only measured spectral/noise augmentation; no claim of simulated metres."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import numpy as np
from data import ROOT,Frontend,framed_example,normalize,load_pcm
from device_domain import pcm


def spectrum(x):
    frames=np.array([x[i:i+512]*np.hanning(512) for i in range(0,len(x)-511,256)])
    if not len(frames):raise ValueError('No complete spectral frame')
    return np.mean(abs(np.fft.rfft(frames,axis=1))**2,axis=0)


def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);args=p.parse_args()
    args.out.mkdir(parents=True,exist_ok=False)
    base=ROOT/'artifacts/kws-phase5/features';meta=json.loads((base/'features.json').read_text(encoding='utf8'))
    reports=[json.loads(Path(name).read_text(encoding='utf8')) for name in meta['source_reports']]
    trials={t['source']['clip_id']:t for report in reports for t in report['trials']}
    frequencies=np.fft.rfftfreq(512,1/16000);edges=np.geomspace(125,7600,21)
    centres=np.sqrt(edges[:-1]*edges[1:]);curves=[];noise=[];used=[]
    for capture in meta['captures']:
        if capture['split']!='train':continue
        trial=trials[capture['parent_clip_id']];row=trial['source']
        recorded=pcm(ROOT/trial['path']).astype(float)
        source_path=Path(trial['path']).parent/'leveled-sources'/(row['clip_id']+'.wav')
        source=pcm(ROOT/source_path).astype(float)*row['playback_gain']
        lag=capture['lag_samples'];actual=recorded[lag:lag+len(source)]
        clean=spectrum(source);observed=spectrum(actual);floor=spectrum(recorded[-16000:])
        curve=[]
        for low,high in zip(edges[:-1],edges[1:]):
            mask=(frequencies>=low)&(frequencies<high)
            power=max(1.,float((observed[mask]-floor[mask]).sum()))
            curve.append(np.clip(10*np.log10(power/max(1.,clean[mask].sum())),-18,18))
        curves.append(curve);noise.append(recorded[-16000:].astype(np.int16));used.append(row['clip_id'])
    median=np.median(curves,axis=0)
    assert len(used)>=20
    stats=json.loads((base/'normalization.json').read_text());frontend=Frontend();rng=np.random.default_rng(20260924)
    rows=[json.loads(line) for line in (ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl').read_text(encoding='utf8').splitlines()]
    rows=[r for r in rows if r['split']=='train']
    added=[];clipped=0
    for index,row in enumerate(rows):
        source=load_pcm(row)
        for variant in range(2):
            signal,labels,end=framed_example(row,source,rng)
            # Use a sampled TRAIN response shrunk toward the robust median.
            response=.5*median+.5*np.asarray(curves[int(rng.integers(len(curves)))])
            response+=rng.normal(0,2,len(response))
            magnitudes=10**(np.interp(frequencies,centres,response)/20)
            impulse=np.roll(np.fft.irfft(magnitudes,n=512),256)*np.hanning(512)
            colored=np.convolve(signal.astype(float),impulse,mode='same')
            ambient=np.resize(noise[int(rng.integers(len(noise)))],len(signal)).astype(float)
            ambient*=10**(rng.uniform(-3,3)/20)
            active=np.abs(colored)>max(1.,np.max(np.abs(colored))*.08)
            speech_rms=np.sqrt(np.mean(colored[active]**2)) if np.any(active) else 1.
            noise_rms=max(1.,np.sqrt(np.mean(ambient**2)))
            snr=rng.uniform(0,22)
            colored*=noise_rms*10**(snr/20)/max(1.,speech_rms)
            combined=colored+ambient;clipped+=int((np.abs(combined)>32767).sum())
            signal=np.clip(np.rint(combined),-32768,32767).astype(np.int16)
            added.append((row,normalize(frontend(signal),stats),labels,end,variant))
        if index%200==0:print(json.dumps(dict(sources_done=index+1,total=len(rows))),flush=True)
    original=np.load(base/'train.npz',allow_pickle=False)
    extra=dict(x=np.stack([e[1] for e in added]),y=np.stack([e[2] for e in added]),
        language=np.array([e[0]['language'] for e in added]),label=np.array([e[0]['label'] for e in added]),
        clip_id=np.array(['channel-'+e[0]['clip_id'] for e in added]),source_group=np.array([e[0]['source_group'] for e in added]),
        event_end=np.array([e[3] if e[3] is not None else -1 for e in added]),
        event_start_accept=np.array([e[3]-(e[0].get('speech_end_high_sample',0)-e[0].get('speech_end_low_sample',0)) if e[3] is not None else -1 for e in added]),
        sampling_group=np.array([e[0].get('sampling_group','natural_negative' if not e[0]['label'] else 'positive') for e in added]),
        text=np.array([e[0].get('text','') for e in added]),variant=np.array([e[4] for e in added]),
        device_domain=np.zeros(len(added),dtype=bool))
    assert set(original.files)==set(extra)
    np.savez_compressed(args.out/'train.npz',**{key:np.concatenate((original[key],extra[key])) for key in extra})
    for name in ('validation.npz','test.npz','normalization.json'):shutil.copyfile(base/name,args.out/name)
    report=dict(complete=True,seed=20260924,synthetic_examples=len(added),train_capture_ids=used,
        centre_frequencies=centres.tolist(),median_response_db=median.tolist(),clipped_samples=clipped,
        snr_db=[0,22],new_device_recordings=False,five_metre_equivalence=False,
        validation_unchanged=hashlib.sha256((base/'validation.npz').read_bytes()).hexdigest(),
        split_sha256={s:hashlib.sha256((args.out/(s+'.npz')).read_bytes()).hexdigest() for s in ('train','validation','test')})
    (args.out/'features.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report),flush=True)


if __name__=='__main__':main()
