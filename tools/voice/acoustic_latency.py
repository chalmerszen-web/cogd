"""Offline source-envelope match and review-only acoustic response candidates.
No USB, playback, ASR, model calls or network. Requires NumPy only.
"""
import argparse
import hashlib
import json
from pathlib import Path
import wave
import numpy as np

STEP=.01

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def envelope(path):
    with wave.open(str(path),'rb') as w:
        if w.getsampwidth()!=2:raise ValueError('Expected signed 16-bit PCM')
        rate=w.getframerate();channels=w.getnchannels()
        x=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(float).reshape(-1,channels).mean(axis=1)
    n=round(rate*STEP);count=len(x)//n
    blocks=x[:count*n].reshape(count,n)
    env=np.sqrt(np.mean((blocks-blocks.mean(axis=1,keepdims=True))**2,axis=1))
    return env,dict(rate=rate,channels=channels,samples=len(x),seconds=len(x)/rate,sha256=sha(path))

def match(reference,recording):
    n=len(reference)
    if n<3 or n>len(recording):raise ValueError('Source does not fit recording')
    ref=reference-reference.mean();power=np.dot(ref,ref)
    if power<=0:raise ValueError('Source has no envelope variation')
    size=1<<(len(recording)+n-2).bit_length()
    cross=np.fft.irfft(np.fft.rfft(recording,size)*np.fft.rfft(ref[::-1],size),size)[n-1:len(recording)]
    sums=np.r_[0,np.cumsum(recording)];squares=np.r_[0,np.cumsum(recording**2)]
    variance=np.maximum(0,squares[n:]-squares[:-n]-(sums[n:]-sums[:-n])**2/n)
    score=cross/np.sqrt(np.maximum(power*variance,1e-20))
    best=int(np.argmax(score));other=score.copy();other[max(0,best-25):best+26]=-1
    return best,float(score[best]),float(other.max()),score

def analyze(trial,folder):
    src,source=envelope(folder/'utterance.wav');rec,recording=envelope(folder/'speaker.wav')
    offset,corr,second,_=match(src,rec)
    threshold=max(64.,float(src.max())*.02);active=np.flatnonzero(src>=threshold)
    if not len(active):raise ValueError('No source activity')
    end=(offset+int(active[-1])+1)*STEP
    row=dict(source=source,recording=recording,source_match=dict(start_s=offset*STEP,correlation=corr,
        competing_correlation=second,accepted=corr>=.75 and corr-second>=.05),
        source_activity_rule='10ms de-DC RMS >= max(64, 2% maximum source RMS)',source_activity_threshold=threshold,
        source_active_end_recording_s=end,metric='acoustic_response_onset',useful_speech_confirmed=False,
        useful_speech_latency_s=None,within_one_second=None,acoustic_response_onset_s=None,
        uncertainty=['10ms envelope grid; source match is not manual speech annotation',
                     'Energy onset may be cue/background; transcript or human review required'],review_intervals=[])
    if not row['source_match']['accepted']:
        row['source_active_end_recording_s']=None;row['error']='Source match weak or ambiguous';return row
    playback=trial.get('utterance_playback',{});host=playback.get('source_started')
    events=trial.get('events',[]);pcm=next((e for e in events if e.get('stage')=='playback'),None)
    speaker=next((e for e in events if e.get('stage')=='speaker_start'),None)
    if host is None or not pcm or 'observed' not in pcm:
        row['error']='Missing timing anchor; no response onset claim';return row
    # Use actual source match for record clock; retrospective speaker telemetry
    # is related by board time, never by its delayed USB receipt.
    anchor=offset*STEP+(pcm['observed']-host)
    if speaker and 'time_ms' in speaker and 'time_ms' in pcm:
        anchor+=(speaker['time_ms']-pcm['time_ms'])/1000
    low=max(end+.05,anchor-.75);high=min(len(rec)*STEP,anchor+3)
    row['telemetry_search_recording_s']=[low,high]
    row['uncertainty'].append('Search anchor includes unknown USB receipt and waveOut scheduling delay; +/-0.75s is a search margin, not a calibrated error bound')
    baseline=rec[max(0,offset-100):offset]
    noise=float(np.median(baseline)) if len(baseline) else float(np.percentile(rec,20))
    gate=max(64.,noise*4);a=max(0,int(low/STEP));b=min(len(rec),int(high/STEP))
    starts=[i for i in range(a,max(a,b-4)) if np.count_nonzero(rec[i:i+5]>=gate)>=4 and (i==a or rec[i-1]<gate)]
    row['candidate_energy_threshold']=gate
    row['candidate_onsets_s']=[i*STEP for i in starts]
    row['review_intervals']=[dict(start_s=max(0,end-.3),end_s=min(len(rec)*STEP,high+.5),file=str(folder/'speaker.wav'))]
    row['excluded_source_interval_s']=[offset*STEP,end]
    row['cue_and_background_exclusion']='Unverified: no independent cue/reply template. Candidates cannot certify speech or <=1s.'
    # A single energy rise still cannot independently identify device speech.
    if starts:row['candidate_response_latency_s']=starts[0]*STEP-end
    else:row['candidate_response_latency_s']=None
    return row

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('report',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():raise FileExistsError(a.output)
    report=json.loads(a.report.read_text(encoding='utf-8-sig'));rows=[]
    for i,trial in enumerate(report.get('trials',[])):
        folder=a.report.parent/f'trial-{i+1:02}'
        try:r=analyze(trial,folder)
        except Exception as e:r=dict(error=type(e).__name__+': '+str(e))
        r.update(trial=i+1,id=trial.get('id'));rows.append(r)
    result=dict(report=str(a.report),report_sha256=sha(a.report),script_sha256=sha(Path(__file__)),numpy=np.__version__,trials=rows,
                note='No useful-speech or one-second acceptance claim without independently reviewed reply identity.')
    a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(dict(trials=len(rows),errors=sum('error' in r for r in rows))))

if __name__=='__main__':main()
