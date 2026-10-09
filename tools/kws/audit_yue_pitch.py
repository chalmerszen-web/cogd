"""Bounded source-pitch diagnostic, not phonetic truth or a relabelling rule."""
import hashlib
import json
from pathlib import Path
import sys

import librosa
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from data import load_pcm,read_manifest


def ordered(rows):return sorted(rows,key=lambda r:hashlib.sha256(r['clip_id'].encode()).hexdigest())


def pitch(signal):
    f0,voiced,prob=librosa.pyin(signal.astype(float)/32768,fmin=65,fmax=500,sr=16000,
        frame_length=1024,hop_length=160,center=False)
    return (np.arange(len(f0))*160+512)/16000,f0,voiced,prob


def main():
    out=ROOT/'artifacts/kws-phase5/yue-pitch-diagnosis'
    out.mkdir(exist_ok=False)
    rows=read_manifest(ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl')
    failed=next(r for r in rows if r['clip_id']=='dataset-yue-zf_xiaoxiao-0-001-bounded1')
    positives=ordered([r for r in rows if r['split']=='validation' and r['language']=='yue'
                       and r['label'] and r['source_group']==failed['source_group']])[:6]
    negatives=ordered([r for r in rows if r['split']=='train' and r['language']=='yue' and r.get('text')=='你好小燕'])
    selected=[failed]+positives
    for negative in negatives:
        selected.append(negative)
        selected.append(ordered([r for r in rows if r['split']=='train' and r['language']=='yue'
                                and r['label'] and r['source_group']==negative['source_group']])[0])
    assert len(selected)==21 and not any(r['split']=='test' for r in selected)
    (out/'selection.json').write_text(json.dumps(selected,ensure_ascii=False,indent=2),encoding='utf8')
    control=[]
    for hz in (120,240):
        signal=(10000*np.sin(2*np.pi*hz*np.arange(16000)/16000)).astype(np.int16)
        _,f0,voiced,prob=pitch(signal)
        measured=float(np.median(f0[voiced&(prob>=.8)]))
        assert abs(measured/hz-1)<.02
        control.append(dict(expected_hz=hz,measured_hz=measured))
    result=[];curves=[]
    for row in selected:
        signal=load_pcm(row)
        times,f0,voiced,prob=pitch(signal)
        end=row['speech_end_low_sample']/16000
        tail=(times>=end-.35)&(times<=end)&voiced&(prob>=.8)&np.isfinite(f0)
        entry=dict(clip_id=row['clip_id'],label=row['label'],text=row.get('text'),split=row['split'],
            source_group=row['source_group'],pcm_sha256=hashlib.sha256(signal.tobytes()).hexdigest(),
            source_wav_sha256=hashlib.sha256((ROOT/row['path']).read_bytes()).hexdigest(),
            earlier_asr=row.get('negative_carrier_asr'),tail_voiced_frames=int(tail.sum()))
        if tail.sum()>=6:
            f=f0[tail];third=max(1,len(f)//3)
            entry.update(tail_median_hz=float(np.median(f)),
                         tail_delta_semitones=float(12*np.log2(np.median(f[-third:])/np.median(f[:third]))))
        else:entry['insufficient_pitch_evidence']=True
        np.savez_compressed(out/(row['clip_id']+'.npz'),times=times,f0=f0,voiced=voiced,probability=prob)
        result.append(entry);curves.append((times-end,np.where(tail,f0,np.nan)))
        print(json.dumps(entry,ensure_ascii=False),flush=True)
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,ax=plt.subplots(figsize=(8,4))
    for index,(x,y) in enumerate(curves[:7]):
        ax.plot(x,y,color='#b3261e' if index==0 else '#657c92',lw=2 if index==0 else 1,
                alpha=1 if index==0 else .5,label='Failing negative' if index==0 else 'Same-voice positives' if index==1 else None)
    ax.set(xlim=(-.35,0),xlabel='Seconds before automatic speech endpoint',ylabel='Estimated F0 (Hz)',
           title='Development audio only: pitch is not a phonetic label')
    ax.legend();fig.tight_layout();fig.savefig(out/'comparison.png',dpi=160);plt.close(fig)
    report=dict(complete=True,librosa=librosa.__version__,controls=control,trials=result,
        source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        selection_sha256=hashlib.sha256((out/'selection.json').read_bytes()).hexdigest(),
        reference='https://librosa.org/doc/0.10.2/generated/librosa.pyin.html',
        limitation='Last350ms relative to an automatic endpoint, not forced syllable alignment. F0 alone cannot establish the spoken word/tone or justify changing a label.',
        labels_changed=False,threshold_changed=False,trained=False,flashed=False)
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')


if __name__=='__main__':main()
