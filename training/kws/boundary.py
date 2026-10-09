"""Fixed energy-supported intervals; crop boundaries never define word ends."""
import numpy as np

CONFIG=dict(version=1,rate=16000,hop=160,minimum_run_frames=3,
    lower_db=30,upper_db=40,absolute_rms_floor=32,maximum_uncertainty_samples=1280)


def sustained(mask,length=3):
    result=np.zeros(len(mask),dtype=bool)
    start=None
    for i,value in enumerate(np.r_[mask,False]):
        if value and start is None:start=i
        if not value and start is not None:
            if i-start>=length:result[start:i]=True
            start=None
    return result


def interval(pcm):
    pcm=np.asarray(pcm,dtype=float)
    if not len(pcm):raise ValueError('empty_audio')
    rms=np.sqrt(np.mean(np.pad(pcm,(0,(-len(pcm))%160)).reshape(-1,160)**2,axis=1))
    peak=float(rms.max())
    if peak<64:raise ValueError('low_energy')
    active=[]
    for db in (30,40):
        mask=sustained(rms>=max(CONFIG['absolute_rms_floor'],peak*10**(-db/20)))
        indices=np.flatnonzero(mask)
        if not len(indices):raise ValueError('no_sustained_speech')
        active.append(indices)
    low=min(len(pcm),int(active[0][-1]+1)*160)
    high=min(len(pcm),int(active[1][-1]+2)*160)
    start=max(0,int(active[1][0])*160-160)
    if high-low>CONFIG['maximum_uncertainty_samples']:raise ValueError('uncertain_end_over_80ms')
    if low<=start or high-start>32768:raise ValueError('word_duration_outside_budget')
    return dict(speech_start_sample=start,speech_end_low_sample=low,speech_end_high_sample=high,
        endpoint_peak_rms=peak,endpoint_method='sustained_energy_30_40db_interval_v1')


def place(row,pcm,rng=None,frames=256):
    """Identical placement for positive and short-negative source utterances."""
    length=frames*256
    low=row['speech_end_low_sample'];high=row['speech_end_high_sample']
    earliest=max(4096,32768+1024-low);latest=length-len(pcm)-1024
    if latest<earliest:raise ValueError('source_context_does_not_fit')
    offset=int(rng.integers(earliest,latest+1)) if rng else (earliest+latest)//2
    signal=np.zeros(length,dtype=np.float64);signal[offset:offset+len(pcm)]=pcm
    labels=np.zeros(frames,dtype=np.float32);end=None
    if row['label']:
        ends=(np.arange(frames)+1)*256;lo=offset+low;end=offset+high
        labels[(ends>=lo)&(ends<end)]=-1
        labels[(ends>=end)&(ends<=end+2560)]=1
    return signal,labels,end
