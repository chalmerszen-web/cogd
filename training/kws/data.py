"""Source-grouped manifests and features from the exact deployed C frontend."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import sys
import wave
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/kws'))
from parity import Backend, Trace
from quantize import requantize

def read_manifest(path):
    rows=[json.loads(line) for line in Path(path).read_text(encoding='utf8').splitlines() if line.strip()]
    seen={}; ids=set()
    for row in rows:
        for field in ('clip_id','path','label','language','source_group','original_recording_id','pcm_sha256','license_id','split'):
            if field not in row or row[field] is None: raise ValueError(f'Missing {field}')
        if row['split'] not in ('train','validation','test'): raise ValueError('Unknown split')
        if row['label'] not in (0,1): raise ValueError('Nonbinary label')
        if row['clip_id'] in ids: raise ValueError('Duplicate clip ID')
        ids.add(row['clip_id'])
        if row['label']:
            if row.get('needs_alignment'): raise ValueError('Carrier sentence has not been aligned')
            if row['language'] not in ('zh','yue'): raise ValueError('Unknown positive language')
            if not 0 <= row['wake_start_sample'] < row['wake_end_sample']:
                raise ValueError('Missing/reversed event boundary')
            if row['wake_end_sample']-row['wake_start_sample']>32768:
                raise ValueError('Positive longer than receptive field; do not truncate the word')
        groups=[('voice',row['source_group']),('recording',row['clip_id']),
                ('recording',row['original_recording_id']),('pcm',row['pcm_sha256'])]
        for field in ('parent_clip_id','noise_parent_id'):
            if row.get(field): groups.append(('recording',row[field]))
        for field in ('source_pcm_sha256','reference_pcm_sha256'):
            if row.get(field): groups.append(('pcm',row[field]))
        for field in ('human_speaker_id','reference_speaker_id'):
            if row.get(field): groups.append(('voice',row[field]))
        for group in groups:
            if group in seen and seen[group]!=row['split']: raise ValueError(f'Source leakage: {group}')
            seen[group]=row['split']
    return rows

def load_pcm(row):
    path=Path(row['path']); path=path if path.is_absolute() else ROOT/path
    with wave.open(str(path),'rb') as stream:
        if (stream.getnchannels(),stream.getsampwidth(),stream.getframerate())!=(1,2,16000):
            raise ValueError('Expected mono PCM16 16 kHz: '+str(path))
        raw=stream.readframes(stream.getnframes())
    if hashlib.sha256(raw).hexdigest()!=row['pcm_sha256']: raise ValueError('PCM hash mismatch')
    if row['label'] and row['wake_end_sample']>len(raw)//2:
        raise ValueError('Positive annotation extends past recording')
    pcm=np.frombuffer(raw,dtype='<i2').copy()
    if (row['label'] or row.get('source_dataset')=='google/fleurs') and (not len(pcm) or np.max(np.abs(pcm.astype(np.int32)))<32):
        raise ValueError('Speech source is effectively silent')
    return pcm

class Frontend:
    def __init__(self,library=ROOT/'build-kws-host/libkws.so'):
        self.backend=Backend(library)
        self.backend.lib.kws_frontend.argtypes=[C.c_void_p,C.POINTER(C.c_int16),C.POINTER(Trace)]
    def __call__(self,pcm):
        backend=self.backend; backend.lib.kws_reset(backend.handle)
        padded=np.pad(np.asarray(pcm,dtype=np.int16),(0,(-len(pcm))%256))
        result=np.empty((len(padded)//256,40),dtype=np.int16)
        trace=Trace()
        for index,frame in enumerate(padded.reshape(-1,256)):
            backend.lib.kws_frontend(backend.handle,frame.ctypes.data_as(C.POINTER(C.c_int16)),C.byref(trace))
            result[index]=trace.logmel
        return result

def normalization(logmel):
    # Input logmel is Q8.8. Only caller-supplied TRAIN samples are permitted.
    values=np.concatenate(logmel).astype(np.float64)/256
    mean=np.sign(values.mean(0))*np.floor(np.abs(values.mean(0)*256)+.5)
    inverse=np.floor(4096/np.maximum(values.std(0),.25)+.5)
    return dict(mean_q8=np.clip(mean,-32768,32767).astype(int).tolist(),
                inverse_std_q12=np.clip(inverse,1,65535).astype(int).tolist())

def normalize(logmel,stats):
    raw=(np.asarray(logmel,dtype=np.int64)-stats['mean_q8'])*stats['inverse_std_q12']
    return requantize(raw,15).astype(np.int8)

def framed_example(row,pcm,rng=None,frames=256,background=None):
    """Place an entire word after warm-up; never crop positive speech."""
    length=frames*256
    boundary_labels=None
    if row.get('endpoint_schema')==1:
        from boundary import place
        signal,boundary_labels,event_end=place(row,pcm,rng,frames)
    elif row['label']:
        start,end=row['wake_start_sample'],row['wake_end_sample']
        word=pcm[start:end]
        latest=length-len(word)-4096
        # Word can begin before warm-up; its complete tail and all labels follow it.
        earliest=max(4096,32768-len(word)+1024)
        if latest<earliest: raise ValueError('Whole word cannot fit training window')
        offset=int(rng.integers(earliest,latest+1)) if rng else (earliest+latest)//2
        signal=np.zeros(length,dtype=np.float64); signal[offset:offset+len(word)]=word
        event_end=offset+len(word)
    else:
        offset=int(rng.integers(0,max(1,len(pcm)-length+1))) if rng else 0
        signal=np.zeros(length,dtype=np.float64); chunk=pcm[offset:offset+length]; signal[:len(chunk)]=chunk
        event_end=None
    if rng is not None:
        signal*=10**(rng.uniform(-14,3)/20)
        if background is not None:
            # Only a caller-selected TRAIN source may be mixed. Measure target
            # RMS over speech, not the deliberately padded silent interval.
            active=signal[signal!=0]
            reference_rms=np.sqrt(np.mean(active**2)) if len(active) else 100.
            noise=np.asarray(background,dtype=np.float64)
            if not len(noise):raise ValueError('Empty augmentation background')
            noise=np.tile(noise,int(np.ceil((length+len(noise))/len(noise))))
            beginning=int(rng.integers(0,len(noise)-length+1));noise=noise[beginning:beginning+length]
            noise_rms=np.sqrt(np.mean(noise**2))
            if noise_rms>0:
                signal+=noise*(reference_rms/max(noise_rms,1)/10**(rng.uniform(10,30)/20))
        if rng.random()<.5:
            original=signal.copy()
            for delay_ms,gain in ((rng.uniform(10,25),.20),(rng.uniform(30,60),.10)):
                delay=int(delay_ms*16);signal[delay:]+=gain*original[:-delay]
        # Synthetic noise is generated independently inside training only.
        signal+=rng.normal(0,rng.uniform(0,80),length)
    pcm=np.clip(np.floor(signal+.5),-32768,32767).astype(np.int16)
    end_samples=(np.arange(frames)+1)*256
    labels=np.zeros(frames,dtype=np.float32)
    if event_end is not None: labels[(end_samples>=event_end)&(end_samples<=event_end+2560)]=1
    if boundary_labels is not None:labels=boundary_labels
    return pcm,labels,event_end

def prepare(rows,out,augmentations=3,seed=20260921):
    if not 0<=augmentations<=8:raise ValueError('Augmentation count outside fixed budget')
    out=Path(out);out.mkdir(parents=True,exist_ok=False)
    frontend=Frontend();rng=np.random.default_rng(seed); examples=[]
    noise_rows=[r for r in rows if r['split']=='train' and not r['label']
                and (r.get('source_dataset')=='google/fleurs' or r.get('source_kind'))]
    noise_cache={};parents=[]
    for row in rows:
        source=load_pcm(row)
        count=augmentations+1 if row['split']=='train' else 1
        for variant in range(count):
            noise=None
            if variant and noise_rows and rng.random()<.5:
                chosen=noise_rows[int(rng.integers(len(noise_rows)))];key=chosen['clip_id']
                if key not in noise_cache:noise_cache[key]=load_pcm(chosen)
                noise=noise_cache[key];parents.append(dict(clip_id=row['clip_id'],variant=variant,noise_parent=key))
            pcm,labels,end=framed_example(row,source,rng if variant else None,background=noise)
            feature=frontend(pcm)
            examples.append((row,feature,labels,end,variant))
    stats=normalization([x[1] for x in examples if x[0]['split']=='train'])
    (out/'normalization.json').write_text(json.dumps(stats,indent=2)+'\n')
    for split in ('train','validation','test'):
        selected=[x for x in examples if x[0]['split']==split]
        if not selected: raise ValueError('Empty '+split+' split')
        np.savez_compressed(out/(split+'.npz'),
            x=np.stack([normalize(x[1],stats) for x in selected]),
            y=np.stack([x[2] for x in selected]),
            language=np.array([x[0]['language'] for x in selected]),
            label=np.array([x[0]['label'] for x in selected]),
            clip_id=np.array([x[0]['clip_id'] for x in selected]),
            source_group=np.array([x[0]['source_group'] for x in selected]),
            event_end=np.array([x[3] if x[3] is not None else -1 for x in selected]),
            event_start_accept=np.array([x[3]-(x[0].get('speech_end_high_sample',0)-x[0].get('speech_end_low_sample',0)) if x[3] is not None else -1 for x in selected]),
            sampling_group=np.array([x[0].get('sampling_group','natural_negative' if not x[0]['label'] else 'positive') for x in selected]),
            text=np.array([x[0].get('text','') for x in selected]),
            variant=np.array([x[4] for x in selected]))
    metadata=dict(seed=seed,rows=len(rows),examples=len(examples),augmentations=augmentations,
                  dataset_sha256=hashlib.sha256(json.dumps(rows,sort_keys=True).encode()).hexdigest(),
                  split_sha256={s:hashlib.sha256((out/(s+'.npz')).read_bytes()).hexdigest()
                                for s in ('train','validation','test')},
                  frontend_sha256=hashlib.sha256((ROOT/'build-kws-host/libkws.so').read_bytes()).hexdigest(),
                  normalization_sources=[r['clip_id'] for r in rows if r['split']=='train'],
                  noise_parents=parents,augmentation=dict(gain_db=[-14,3],background_snr_db=[10,30],
                      background_split='train',short_reflection_probability=.5,gaussian_rms=[0,80]))
    (out/'features.json').write_text(json.dumps(metadata,indent=2)+'\n')
    return metadata
