"""Offline acoustic latency candidates; no hardware, network or ASR execution.
Run with the existing .local/tts-python environment (NumPy/SciPy).
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import numpy as np
from scipy import signal
from scipy.io import wavfile

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def load(path):
    rate,x=wavfile.read(path)
    if x.dtype!=np.int16:raise ValueError('Expected signed 16-bit WAV')
    x=x.astype(float)/32768
    if x.ndim==2:x=x.mean(axis=1)
    g=math.gcd(rate,16000)
    return signal.resample_poly(x,16000//g,rate//g)
def envelope(x):
    return np.sqrt(np.mean(x[:len(x)//160*160].reshape(-1,160)**2,axis=1))
def correlate(x,y):
    n=len(x)
    if n<3 or n>len(y):raise ValueError('Source must fit recording')
    x=x-x.mean();power=np.dot(x,x)
    if power<=0:raise ValueError('No source variation')
    sums=np.r_[0,np.cumsum(y)];squares=np.r_[0,np.cumsum(y*y)]
    variance=np.maximum(0,squares[n:]-squares[:-n]-(sums[n:]-sums[:-n])**2/n)
    return signal.correlate(y,x,mode='valid',method='fft')/np.sqrt(np.maximum(power*variance,1e-25))
def align(x,y):
    rows=[]
    for low,high in [(300,1000),(1000,2000),(2000,3500)]:
        sos=signal.butter(4,[low,high],fs=16000,btype='bandpass',output='sos')
        u=signal.sosfiltfilt(sos,x);v=signal.sosfiltfilt(sos,y)
        for method,a,b,rate in [('waveform',u,v,16000),('envelope',envelope(u),envelope(v),100)]:
            c=correlate(a,b);i=int(np.argmax(c));other=c.copy();width=round(.25*rate)
            other[max(0,i-width):i+width+1]=-1
            rows.append(dict(band_hz=[low,high],method=method,start_s=i/rate,correlation=float(c[i]),competing_correlation=float(other.max())))
    env=[r for r in rows if r['method']=='envelope'];offset=float(np.median([r['start_s'] for r in env]))
    accepted=all(r['correlation']>=.75 and r['correlation']-r['competing_correlation']>=.05 for r in env) and max(r['start_s'] for r in env)-min(r['start_s'] for r in env)<=.03
    return dict(offset_s=offset,accepted=accepted,bands=rows)
def transcripts(directory):
    p=directory/'external-asr.json'
    if p.exists():
        return {r['trial']:{'text':r.get('transcript'),'start_s':r.get('start_seconds'),'evidence':str(p),'sha256':sha(p)} for r in json.loads(p.read_text(encoding='utf-8-sig'))}
    # Existing independently computed offline ASR, never firmware reply text.
    p=directory/'independent-acoustic.json'
    if p.exists():
        return {i+1:dict(r['reply_asr'],evidence=str(p),sha256=sha(p)) for i,r in enumerate(json.loads(p.read_text(encoding='utf8'))['trials']) if 'reply_asr' in r}
    return {}
def analyze(trial,folder,transcript):
    source=folder/'utterance.wav';recording=folder/'speaker.wav'
    x=load(source);y=load(recording);match=align(x,y)
    row=dict(id=trial.get('id'),source=str(source),source_sha256=sha(source),recording=str(recording),recording_sha256=sha(recording),original_source=trial.get('utterance_source'),source_match=match,transcript_evidence=transcript,metric='acoustic_response_onset_candidate',useful_answer_onset_s=None,within_one_second=None,review_required=True)
    if not match['accepted']:return dict(row,status='unknown',reason='Weak or ambiguous source match')
    e=envelope(x);active=np.flatnonzero(e>=max(64/32768,e.max()*.02))
    if not len(active):return dict(row,status='unknown',reason='No source activity')
    source_end=match['offset_s']+(int(active[-1])+1)*.01
    events=trial.get('events',[]);pcm=next((v for v in events if v.get('stage')=='playback'),None)
    host=trial.get('utterance_playback',{}).get('source_started')
    row['source_activity_end_recording_s']=source_end
    if not pcm or host is None:return dict(row,status='unknown',reason='No event search anchor')
    anchor=match['offset_s']+pcm['observed']-host
    # Search before the packet-observed anchor, including potential overlapping
    # response. Never silently exclude an early answer using source_end.
    low=max(0,anchor-.75);high=min(len(y)/16000,anchor+3)
    z=envelope(y);lo=round(low*100);hi=min(len(z),round(high*100));noise=float(np.median(z[max(0,lo-50):lo]));gate=max(64/32768,noise*4)
    starts=[k for k in range(lo,max(lo,hi-4)) if np.count_nonzero(z[k:k+5]>gate)>=4 and (k==lo or z[k-1]<=gate)]
    onset=starts[0]*.01 if starts else None
    row.update(source_calibrated_event_window_s=[low,high],event_anchor_recording_s=anchor,candidate_onsets_s=[k*.01 for k in starts],energy_threshold=gate,review_interval_s=[max(0,(onset or anchor)-.2),min(len(y)/16000,(onset or anchor)+2)],source_activity_rule='10ms RMS >= max(64/32768, 2% peak RMS)',uncertainty='10ms energy grid; +/-80ms review margin is not calibrated confidence. Event window includes unknown USB/waveOut timing. Cues/background and exact first useful phoneme require review.')
    if onset is None:return dict(row,status='unknown',reason='No sustained energy onset in search window')
    if onset<=source_end+.05:return dict(row,status='unknown',reason='Candidate overlaps source/tail; cannot separate early reply',candidate_response_onset_s=onset)
    latency=onset-source_end
    row.update(status='candidate',candidate_response_onset_s=onset,acoustic_response_onset_latency_s=latency,review_latency_range_s=[latency-.08,latency+.08])
    if not transcript or not transcript.get('text','').strip(' 。.!！?？'):
        row.update(status='unknown',reason='No offline transcript evidence for response identity')
    return row

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():raise FileExistsError(a.output)
    source=a.directory/'report.json';report=json.loads(source.read_text(encoding='utf-8-sig'))
    text=transcripts(a.directory);rows=[]
    for i,t in enumerate(report['trials']):
        try:r=analyze(t,a.directory/f'trial-{i+1:02}',text.get(i+1))
        except Exception as e:r=dict(id=t.get('id'),status='unknown',reason=type(e).__name__+': '+str(e))
        r['conversation_checks_passed']=bool(t.get('complete'))
        r['conversation_error']=t.get('error')
        rows.append(r)
    result=dict(report_sha256=sha(source),script_sha256=sha(Path(__file__)),trials=rows,note='Source-correlated recording clock only; no packet latency acceptance. Original files unchanged.')
    a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps([dict(id=r['id'],status=r['status'],latency=r.get('acoustic_response_onset_latency_s')) for r in rows]))
if __name__=='__main__':main()
