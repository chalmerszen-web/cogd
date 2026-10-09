"""Append audited TRAIN recordings without changing validation/test inputs."""
import argparse
import hashlib
import json
from pathlib import Path,PureWindowsPath
import shutil
import numpy as np
from data import ROOT,Frontend,normalize,framed_example,read_manifest,load_pcm
from device_domain import pcm
from paired_negative import incomplete


def respeed(signal,row,speed):
    if not .85<=speed<=1.15:raise ValueError('Speed outside fixed augmentation range')
    result=np.interp(np.arange(round(len(signal)/speed))*speed,np.arange(len(signal)),signal)
    moved=dict(row)
    for key in ('speech_start_sample','speech_end_low_sample','speech_end_high_sample'):
        moved[key]=round(row[key]/speed)
    if moved['speech_end_high_sample']-moved['speech_start_sample']>32768:
        raise ValueError('Whole phrase exceeds receptive-field budget after speed change')
    return np.clip(np.rint(result),-32768,32767).astype(np.int16),moved


def capture_path(value):
    path=Path(value)
    windows=PureWindowsPath(value)
    if windows.drive and ROOT.as_posix().startswith('/mnt/'):
        path=Path('/mnt',windows.drive[0].lower(),*windows.parts[1:])
    return path if path.is_absolute() else ROOT/path


def main():
    p=argparse.ArgumentParser();p.add_argument('--corpus',type=Path,required=True)
    p.add_argument('--audit',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--base',type=Path,default=ROOT/'artifacts/kws-phase5/features-paired')
    p.add_argument('--seed',type=int,default=20260927)
    args=p.parse_args();audit=json.loads(args.audit.read_text(encoding='utf8'))
    report=json.loads((args.corpus/'report.json').read_text(encoding='utf8'))
    assert audit['complete'] and report['complete']
    assert hashlib.sha256((args.corpus/'report.json').read_bytes()).hexdigest()==audit['source_report_sha256']
    eligible=[r for r in audit['trials'] if r['eligible']]
    assert len(eligible)>=24,'Insufficient audited captures; do not train'
    for language in ('zh','yue'):
        assert sum(r['language']==language and r['label'] for r in eligible)>=6
        assert sum(r['language']==language and not r['label'] for r in eligible)>=3
    args.out.mkdir(parents=True,exist_ok=False)
    base=args.base
    original=np.load(base/'train.npz');validation=np.load(base/'validation.npz');test=np.load(base/'test.npz')
    forbidden=set(validation['source_group'])|set(test['source_group'])
    parents={r['clip_id']:r for r in read_manifest(ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl')}
    trials={t['source']['clip_id']:t for t in report['trials']};rng=np.random.default_rng(args.seed)
    frontend=Frontend();stats=json.loads((base/'normalization.json').read_text())
    examples=[];ancestry=[];rejected=[]
    for accepted in eligible:
        trial=trials[accepted['clip_id']];parent=parents[accepted['parent_clip_id']]
        assert parent['split']=='train' and parent['source_group'] not in forbidden
        source=load_pcm(parent);recorded=pcm(capture_path(trial['path']))
        assert hashlib.sha256(recorded.tobytes()).hexdigest()==trial['signal']['sha256']
        lag=accepted['lag_samples'];start=max(0,lag-1600);stop=min(len(recorded),lag+len(source)+1600)
        signal=recorded[start:stop];noise=recorded[-16000:]
        row=dict(parent,clip_id='device-extra-'+accepted['clip_id'],
            speech_start_sample=lag-start+parent['speech_start_sample'],
            speech_end_low_sample=lag-start+parent['speech_end_low_sample']-320,
            speech_end_high_sample=lag-start+parent['speech_end_high_sample']+320,
            sampling_group='positive' if parent['label'] else 'hard_negative')
        assert 0<=row['speech_start_sample']<row['speech_end_low_sample']<=row['speech_end_high_sample']<=len(signal)
        variants=[(row,signal)]
        if row['label']:
            for side in ('prefix','suffix'):
                partial,_=incomplete(signal,row['speech_start_sample'],row['speech_end_low_sample'],side,ambient=noise)
                variants.append((dict(row,clip_id=row['clip_id']+'-'+side,label=0,
                    text='time-cut-'+side,sampling_group='paired_negative'),partial))
        for variant_row,wave in variants:
            for variant,speed in enumerate((1.,.85,1.15)):
                try:changed,moved=respeed(wave,variant_row,speed)
                except ValueError as error:
                    rejected.append(dict(clip_id=variant_row['clip_id'],speed=speed,reason=str(error)));continue
                # Match source placement for positives/negatives; ambient remains
                # present before and after the actual measured signal.
                framed,labels,end=framed_example(moved,changed)
                earliest=max(4096,33792-moved['speech_end_low_sample']);latest=len(framed)-len(changed)-1024
                offset=(earliest+latest)//2;floor=np.resize(noise,len(framed))
                framed[:offset]=floor[:offset];framed[offset+len(changed):]=floor[offset+len(changed):]
                if variant:
                    gain=10**(rng.uniform(-3,3)/20)
                    framed=np.clip(np.rint(framed.astype(float)*gain),-32768,32767).astype(np.int16)
                examples.append((moved,normalize(frontend(framed),stats),labels,end,variant))
        quiet=dict(row,clip_id=row['clip_id']+'-ambient',label=0,text='',sampling_group='natural_negative')
        examples.append((quiet,normalize(frontend(np.resize(noise,65536)),stats),np.zeros(256,dtype=np.float32),None,0))
        ancestry.append(dict(clip_id=row['clip_id'],parent_clip_id=parent['clip_id'],source_group=parent['source_group'],
            pcm_sha256=trial['signal']['sha256'],recording=trial['path'],gain=accepted['gain'],split='train'))
    extra=dict(x=np.stack([e[1] for e in examples]),y=np.stack([e[2] for e in examples]),
        label=np.array([e[0]['label'] for e in examples]),language=np.array([e[0]['language'] for e in examples]),
        clip_id=np.array([e[0]['clip_id'] for e in examples]),source_group=np.array([e[0]['source_group'] for e in examples]),
        event_end=np.array([e[3] if e[3] is not None else -1 for e in examples]),
        event_start_accept=np.array([e[3]-(e[0]['speech_end_high_sample']-e[0]['speech_end_low_sample']) if e[3] is not None else -1 for e in examples]),
        sampling_group=np.array([e[0]['sampling_group'] for e in examples]),text=np.array([e[0]['text'] for e in examples]),
        variant=np.array([e[4] for e in examples]),device_domain=np.ones(len(examples),dtype=bool))
    assert set(extra)==set(original.files)
    assert not set(extra['clip_id'])&set(original['clip_id'])
    np.savez_compressed(args.out/'train.npz',**{k:np.concatenate((original[k],extra[k])) for k in extra})
    for name in ('validation.npz','test.npz','normalization.json'):shutil.copyfile(base/name,args.out/name)
    metadata=dict(complete=True,captures=len(eligible),examples=len(examples),seed=args.seed,
        base_directory=str(base),base_metadata_sha256=hashlib.sha256((base/'features.json').read_bytes()).hexdigest(),
        ancestry=ancestry,rejected_speed_variants=rejected,
        audit_sha256=hashlib.sha256(args.audit.read_bytes()).hexdigest(),
        split_sha256={s:hashlib.sha256((args.out/(s+'.npz')).read_bytes()).hexdigest() for s in ('train','validation','test')},
        original_train_sha256=hashlib.sha256((base/'train.npz').read_bytes()).hexdigest(),
        validation_unchanged=hashlib.sha256((base/'validation.npz').read_bytes()).hexdigest(),
        test_unchanged=hashlib.sha256((base/'test.npz').read_bytes()).hexdigest(),
        limitation='Synthetic-source replay in one room; not a five-metre or human-generalization proof.')
    (args.out/'features.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(dict(complete=True,captures=len(eligible),examples=len(examples),rejected_variants=len(rejected))))


if __name__=='__main__':main()
