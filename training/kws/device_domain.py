"""Append source-grouped board captures to frozen features for device adaptation.

Alignment uses development-source energy envelopes, with a bounded uncertainty
mask. No held-out test recordings are used for normalization or training.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import wave
import numpy as np
from data import ROOT,Frontend,framed_example,normalize


def pcm(path):
    with wave.open(str(path),'rb') as f:
        assert (f.getframerate(),f.getnchannels(),f.getsampwidth())==(16000,1,2)
        return np.frombuffer(f.readframes(f.getnframes()),dtype='<i2').copy()


def envelope(x):
    return np.sqrt(np.mean(np.pad(x.astype(float),(0,(-len(x))%160)).reshape(-1,160)**2,axis=1))


def align(source,recorded,expected_start=None):
    a=envelope(source);b=envelope(recorded);candidates=[]
    if np.std(a)<1:raise ValueError('No varying source envelope')
    lower,upper=(25,131) if expected_start is None else (max(0,int(expected_start/160)-4),int(expected_start/160)+17)
    for lag in range(lower,upper):
        window=b[lag:lag+len(a)]
        if len(window)==len(a) and np.std(window)>1:
            candidates.append((float(np.corrcoef(a,window)[0,1]),lag*160))
    correlation,lag=max(candidates)
    if correlation<.40:raise ValueError(f'Unreliable alignment {correlation:.3f}')
    return lag,correlation


def align_waveform(source,recorded,expected_start):
    """Bounded GCC-PHAT fallback for replay whose RMS envelope is noise-masked.

    This estimates a replay delay, not phonetic boundaries or recognition.
    Require a sharp peak well above the bounded unrelated-correlation floor.
    """
    a=np.asarray(source,dtype=float);b=np.asarray(recorded,dtype=float)
    if len(a)<512 or len(b)<len(a) or np.std(a)<1:raise ValueError('Invalid waveform alignment input')
    n=1<<(len(a)+len(b)-1).bit_length()
    cross=np.fft.rfft(b-b.mean(),n)*np.conj(np.fft.rfft(a-a.mean(),n))
    correlation=np.abs(np.fft.irfft(cross/np.maximum(np.abs(cross),1e-6),n))
    lo=max(0,round(expected_start)-640);hi=min(len(b)-len(a)+1,round(expected_start)+2560)
    if hi-lo<512:raise ValueError('Insufficient bounded lag window')
    window=correlation[lo:hi];at=int(np.argmax(window));peak=float(window[at])
    excluded=np.ones(len(window),dtype=bool);excluded[max(0,at-80):at+81]=False
    floor=window[excluded]
    z=peak/max(1e-12,float(np.sqrt(np.mean(floor**2))))
    ratio=peak/max(1e-12,float(floor.max()))
    if z<7.5 or ratio<1.2:raise ValueError(f'Unreliable waveform peak z={z:.2f}, ratio={ratio:.2f}')
    return lo+at,dict(method='bounded_gcc_phat',peak_rms_ratio=z,peak_competitor_ratio=ratio,
                     expected_start=expected_start,search=[lo,hi])


def main():
    p=argparse.ArgumentParser();p.add_argument('--corpus',type=Path,required=True)
    p.add_argument('--supplement',type=Path)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--seed',type=int,default=20260923)
    args=p.parse_args();report=json.loads((args.corpus/'report.json').read_text(encoding='utf8'))
    assert report['complete'] and len(report['trials'])==64
    source_reports=[args.corpus/'report.json']
    if args.supplement:
        supplemental=json.loads((args.supplement/'report.json').read_text(encoding='utf8'))
        assert supplemental['complete'] and len(supplemental['trials'])==12
        assert all(t['source']['language']=='zh' and not t['source']['label'] for t in supplemental['trials'])
        report['trials'].extend(supplemental['trials']);source_reports.append(args.supplement/'report.json')
    assert len({t['source']['clip_id'] for t in report['trials']})==len(report['trials'])
    voices={s:{t['source']['source_group'] for t in report['trials'] if t['source']['split']==s} for s in ('train','validation')}
    assert not voices['train']&voices['validation'],'Device source voices overlap splits'
    args.out.mkdir(parents=True,exist_ok=False)
    base=ROOT/'artifacts/kws-phase3/features';stats=json.loads((base/'normalization.json').read_text())
    frontend=Frontend();rng=np.random.default_rng(args.seed)
    prepared=[];audit=[];rejected=[];train_latency=[];weak_validation=[]
    for trial in report['trials']:
        row=trial['source'];source=pcm(ROOT/row['path']);recorded=pcm(ROOT/trial['path'])
        assert hashlib.sha256((ROOT/row['path']).read_bytes()).hexdigest()==row['source_wav_sha256']
        assert hashlib.sha256(recorded.tobytes()).hexdigest()==trial['signal']['sha256']
        expected_start=trial['ready']['capture_samples']+16000*(trial['audio']['source_started']-trial['ready_query_finished'])
        uncertainty=320;annotation='source endpoint plus envelope lag, ±20ms uncertainty'
        try:lag,correlation=align(source,recorded,expected_start)
        except ValueError as error:
            if row['split']=='train':
                rejected.append(dict(clip_id=row['clip_id'],reason=str(error)));continue
            # Never remove difficult validation speech merely because it is
            # weak. Keep it with a wider, explicitly estimated time interval.
            assert train_latency
            lag=round((expected_start+float(np.median(train_latency)))/160)*160
            correlation=None;uncertainty=1280
            annotation='host start plus TRAIN median latency, ±80ms uncertainty'
            weak_validation.append(dict(clip_id=row['clip_id'],reason=str(error)))
        if row['split']=='train':train_latency.append(lag-expected_start)
        start=max(0,lag-1600);end=min(len(recorded),lag+len(source)+1600)
        capture=recorded[start:end]
        low=lag-start+(row.get('speech_end_low_sample') or len(source))-uncertainty
        high=lag-start+(row.get('speech_end_high_sample') or len(source))+uncertainty
        assert 0<low<=high<=len(capture)
        derived=dict(row,clip_id='device-'+row['clip_id'],endpoint_schema=1,
                     speech_end_low_sample=low,speech_end_high_sample=high,
                     sampling_group='positive' if row['label'] else 'hard_negative')
        noise=recorded[-16000:].copy()
        framed_example(derived,capture)  # Verify whole-source fit before producing features.
        prepared.append((derived,capture,noise))
        audit.append(dict(clip_id=derived['clip_id'],parent_clip_id=row['clip_id'],split=row['split'],
            source_group=row['source_group'],recording=trial['path'],pcm_sha256=trial['signal']['sha256'],
            lag_samples=lag,correlation=correlation,crop=[start,end],end_interval=[low,high],
            playback_gain=row['playback_gain'],annotation=annotation))
    train_noise=[noise for row,_,noise in prepared if row['split']=='train']
    assert train_noise
    examples=[]
    for row,capture,noise in prepared:
        for variant in range(9 if row['split']=='train' else 1):
            background=train_noise[int(rng.integers(len(train_noise)))] if row['split']=='train' else noise
            signal,labels,end=framed_example(row,capture,rng if variant else None,background=background)
            if not variant:
                # Preserve ambient during artificial placement gaps. Do not
                # replace the actual speech/noise already inside the capture.
                ambient=np.resize(background,len(signal))
                earliest=max(4096,33792-row['speech_end_low_sample'])
                latest=len(signal)-len(capture)-1024;offset=(earliest+latest)//2
                signal[:offset]=ambient[:offset]
                signal[offset+len(capture):]=ambient[offset+len(capture):]
            feature=normalize(frontend(signal),stats)
            examples.append((row,feature,labels,end,variant))
        # Captured late-room background is a negative from the same split/source.
        quiet=np.resize(noise,256*256).astype(np.int16)
        quiet_row=dict(row,clip_id=row['clip_id']+'-ambient',label=0,text='',sampling_group='natural_negative')
        examples.append((quiet_row,normalize(frontend(quiet),stats),np.zeros(256,dtype=np.float32),None,0))
    for split in ('train','validation','test'):
        original=np.load(base/(split+'.npz'),allow_pickle=False)
        added=[e for e in examples if e[0]['split']==split]
        merged={key:original[key] for key in original.files}
        merged['device_domain']=np.zeros(len(original['x']),dtype=bool)
        if added:
            additional=dict(x=np.stack([e[1] for e in added]),y=np.stack([e[2] for e in added]),
                language=np.array([e[0]['language'] for e in added]),label=np.array([e[0]['label'] for e in added]),
                clip_id=np.array([e[0]['clip_id'] for e in added]),source_group=np.array([e[0]['source_group'] for e in added]),
                event_end=np.array([e[3] if e[3] is not None else -1 for e in added]),
                event_start_accept=np.array([e[3]-(e[0]['speech_end_high_sample']-e[0]['speech_end_low_sample']) if e[3] is not None else -1 for e in added]),
                sampling_group=np.array([e[0]['sampling_group'] for e in added]),text=np.array([e[0].get('text','') for e in added]),
                variant=np.array([e[4] for e in added]),device_domain=np.ones(len(added),dtype=bool))
            assert set(additional)==set(merged)
            merged={k:np.concatenate((merged[k],additional[k])) for k in merged}
        np.savez_compressed(args.out/(split+'.npz'),**merged)
    shutil.copyfile(base/'normalization.json',args.out/'normalization.json')
    manifest=dict(seed=args.seed,complete=True,prepared=len(prepared),rejected=rejected,
        weak_validation_retained=weak_validation,captures=audit,
        normalization='unchanged Phase3 train-only normalization',
        source_report_sha256=hashlib.sha256((args.corpus/'report.json').read_bytes()).hexdigest(),
        source_reports={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_reports},
        split_sha256={s:hashlib.sha256((args.out/(s+'.npz')).read_bytes()).hexdigest() for s in ('train','validation','test')},
        limitation='Same room; synthetic replay source speakers; not real five-metre validation')
    (args.out/'features.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(dict(prepared=len(prepared),rejected=rejected,examples=len(examples))),flush=True)


if __name__=='__main__':main()
