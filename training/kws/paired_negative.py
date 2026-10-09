"""Incomplete phrases derived only within each source's development split.

Time cuts are deliberately not claimed to be phonetic word alignments. They
remove a substantial part of a four-syllable positive, preserve its acoustic
identity, and provide harder counterexamples than standalone TTS half words.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import numpy as np
from data import ROOT, Frontend, framed_example, normalize, load_pcm, read_manifest
from device_domain import pcm


def incomplete(signal, start, end, side, fraction=.5, ambient=None):
    if not 0 <= start < end <= len(signal) or side not in ('prefix','suffix'):
        raise ValueError('Invalid interval or side')
    if not .4 <= fraction <= .6:
        raise ValueError('Cut outside incomplete-phrase budget')
    cut=round(start+(end-start)*fraction)
    background=np.zeros(len(signal)) if ambient is None else np.resize(ambient,len(signal)).astype(float)
    mask=np.zeros(len(signal))
    fade=min(320,cut-start,end-cut)
    if side=='prefix':
        mask[:cut]=1
        mask[cut-fade:cut]=np.linspace(1,0,fade)
    else:
        mask[cut:]=1
        mask[cut:cut+fade]=np.linspace(0,1,fade)
    result=signal.astype(float)*mask+background*(1-mask)
    return np.clip(np.rint(result),-32768,32767).astype(np.int16),cut


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    base=ROOT/'artifacts/kws-phase5/features-channel'
    device_meta=json.loads((ROOT/'artifacts/kws-phase5/features/features.json').read_text(encoding='utf8'))
    rows=read_manifest(ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl')
    parents={r['clip_id']:r for r in rows}
    sources=[(r,load_pcm(r),False,None) for r in rows if r['label'] and r['split'] in ('train','validation')]
    noise=[]
    for capture in device_meta['captures']:
        recording=pcm(ROOT/capture['recording']);ambient=recording[-16000:]
        if capture['split']=='train':noise.append(ambient)
        parent=parents[capture['parent_clip_id']]
        if not parent['label']:continue
        low,high=capture['end_interval'];lo,hi=capture['crop']
        row=dict(parent,clip_id=capture['clip_id'],
            speech_start_sample=max(0,capture['lag_samples']-lo+parent['speech_start_sample']),
            speech_end_low_sample=low,speech_end_high_sample=high)
        sources.append((row,recording[lo:hi],True,ambient))
    stats=json.loads((base/'normalization.json').read_text())
    channel=json.loads((base/'features.json').read_text())
    freq=np.fft.rfftfreq(512,1/16000)
    response=10**(np.interp(freq,channel['centre_frequencies'],channel['median_response_db'])/20)
    impulse=np.roll(np.fft.irfft(response,n=512),256)*np.hanning(512)
    frontend=Frontend();rng=np.random.default_rng(20260925);added=[];audit=[]
    for index,(parent,signal,device,ambient) in enumerate(sources):
        for side in ('prefix','suffix'):
            for variant in range(2 if parent['split']=='train' else 1):
                fraction=.5 if not variant else float(rng.uniform(.4,.6))
                part,cut=incomplete(signal,parent['speech_start_sample'],parent['speech_end_low_sample'],side,fraction,ambient)
                row=dict(parent,label=0,sampling_group='paired_negative',text='time-cut-'+side,
                         clip_id=parent['clip_id']+'-'+side)
                wave,labels,end=framed_example(row,part,rng if variant else None)
                if device:
                    # Keep captured room floor through placement gaps too.
                    earliest=max(4096,33792-row['speech_end_low_sample'])
                    latest=len(wave)-len(part)-1024
                    if not variant:
                        offset=(earliest+latest)//2
                        floor=np.resize(ambient,len(wave))
                        wave[:offset]=floor[:offset];wave[offset+len(part):]=floor[offset+len(part):]
                elif variant:
                    colored=np.convolve(wave.astype(float),impulse,mode='same')
                    floor=np.resize(noise[int(rng.integers(len(noise)))],len(wave)).astype(float)
                    active=np.abs(colored)>max(1.,np.max(np.abs(colored))*.08)
                    rms=np.sqrt(np.mean(colored[active]**2)) if np.any(active) else 1.
                    colored*=max(1.,np.sqrt(np.mean(floor**2)))*10**(rng.uniform(0,22)/20)/max(1.,rms)
                    wave=np.clip(np.rint(colored+floor),-32768,32767).astype(np.int16)
                assert end is None and not np.any(labels)
                added.append((row,normalize(frontend(wave),stats),device,variant))
                audit.append(dict(clip_id=row['clip_id'],parent_clip_id=parent['clip_id'],
                    split=parent['split'],source_group=parent['source_group'],side=side,
                    cut=cut,fraction=fraction,device=device,variant=variant))
        if index%200==0:print(json.dumps(dict(sources_done=index+1,total=len(sources))),flush=True)
    for split in ('train','validation'):
        original=np.load(base/(split+'.npz'),allow_pickle=False)
        examples=[e for e in added if e[0]['split']==split];n=len(examples)
        extra=dict(x=np.stack([e[1] for e in examples]),y=np.zeros((n,256),dtype=np.float32),
            label=np.zeros(n,dtype=original['label'].dtype),language=np.array([e[0]['language'] for e in examples]),
            clip_id=np.array([e[0]['clip_id'] for e in examples]),source_group=np.array([e[0]['source_group'] for e in examples]),
            event_end=np.full(n,-1),event_start_accept=np.full(n,-1),sampling_group=np.full(n,'paired_negative'),
            text=np.array([e[0]['text'] for e in examples]),variant=np.array([e[3] for e in examples]),
            device_domain=np.array([e[2] for e in examples]))
        assert set(extra)==set(original.files)
        np.savez_compressed(args.out/(split+'.npz'),**{k:np.concatenate((original[k],extra[k])) for k in extra})
    for name in ('test.npz','normalization.json'):shutil.copyfile(base/name,args.out/name)
    report=dict(complete=True,seed=20260925,examples=len(added),ancestry=audit,
        original_validation_preserved_as_prefix=True,phonetic_boundary_verified=False,
        source_split_hashes={s:hashlib.sha256((base/(s+'.npz')).read_bytes()).hexdigest() for s in ('train','validation','test')},
        split_sha256={s:hashlib.sha256((args.out/(s+'.npz')).read_bytes()).hexdigest() for s in ('train','validation','test')})
    (args.out/'features.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(dict(complete=True,examples=len(added))),flush=True)


if __name__=='__main__':main()
