"""Add screened native TRAIN utterances, tempo/pitch and P4 channel variants."""
import argparse,hashlib,json,sys
from pathlib import Path
import numpy as np
from wake_prepare import ROOT,pcm,bounds,screened
sys.path.insert(0,str(ROOT/'training/kws'))
from paired_negative import incomplete

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def render(base,out):
    import librosa,soundfile as sf
    out.mkdir(parents=True,exist_ok=False)
    source=base/'qwen-native-joint-train/manifest.jsonl';audit=base/'native-joint-train-asr.json'
    rows=[json.loads(s) for s in source.read_text(encoding='utf8').splitlines()]
    checks={r['clip_id']:r['asr_text'] for r in json.loads(audit.read_text(encoding='utf8'))['clips']}
    accepted=[];rejected=[]
    for r in rows:
        assert r['split']=='train' and '-jt-' in r['clip_id']
        if not screened(r,checks[r['clip_id']]):
            rejected.append({'id':r['clip_id'],'reason':'automatic ASR screening','asr':checks[r['clip_id']]});continue
        raw=pcm(r['path'])
        for tempo,pitch in ((1.,0.),(.85,0.),(1.18,0.),(1.,-1.5),(1.,1.5)):
            signal=raw.astype(np.float32)/32768
            if tempo!=1:signal=librosa.effects.time_stretch(signal,rate=tempo)
            if pitch:signal=librosa.effects.pitch_shift(signal,sr=16000,n_steps=pitch)
            raw2=np.clip(np.rint(signal*32768),-32768,32767).astype('<i2')
            try:lo,hi=bounds(raw2)
            except ValueError:
                rejected.append({'id':r['clip_id'],'tempo':tempo,'pitch':pitch,'reason':'duration bounds'});continue
            ident=f"joint-{r['clip_id']}-t{tempo}-p{pitch}";path=out/(ident+'.wav');sf.write(path,raw2,16000,subtype='PCM_16')
            accepted.append(dict(r,clip_id=ident,path=path.relative_to(ROOT).as_posix(),
                parent_clip_id=r['clip_id'],parent_wav_sha256=sha(ROOT/r['path']),wake_start_sample=lo,wake_end_sample=hi,
                p4_domain=False,new_source=True,tempo=tempo,pitch=pitch,wav_sha256=sha(path),
                pcm_sha256=hashlib.sha256(raw2.tobytes()).hexdigest(),seconds=len(raw2)/16000))
    assert len(accepted)>=50
    (out/'manifest.jsonl').write_text(''.join(json.dumps(r,ensure_ascii=False)+'\n' for r in accepted),encoding='utf8')
    report={'complete':True,'input_sources':len(rows),'accepted_variants':len(accepted),'accepted_unique_sources':len({r['parent_clip_id'] for r in accepted}),'rejected':rejected,'source_sha256':sha(source),'asr_sha256':sha(audit),'test_read':False}
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(report,ensure_ascii=False))

def features(base,rendered,out):
    from data import Frontend,normalize
    out.mkdir(parents=True,exist_ok=False);previous=base/'features-v9'
    datasets={s:dict(np.load(previous/(s+'.npz'),allow_pickle=False)) for s in ('train','validation')}
    norm=json.loads((previous/'normalization.json').read_text(encoding='utf8'));front=Frontend();rng=np.random.default_rng(2026110121)
    channel=json.loads((previous/'channel.json').read_text(encoding='utf8'));freq=np.fft.rfftfreq(512,1/16000)
    captured=json.loads((base/'capture-round1-failures/capture-report.json').read_text(encoding='utf8'));assert captured['complete']
    noise=[pcm(t['path'])[:4000].astype(float) for t in captured['trials']]
    rows=[json.loads(s) for s in (rendered/'manifest.jsonl').read_text(encoding='utf8').splitlines()]
    extra=[]
    for row in rows:
        assert row['split']=='train' and '-jt-' in row['clip_id']
        signal=pcm(row['path']);variants=[(row,signal)]
        if row['label']:
            for side in ('prefix','suffix'):
                part,_=incomplete(signal,row['wake_start_sample'],row['wake_end_sample'],side)
                variants.append((dict(row,label=0,clip_id=row['clip_id']+'-'+side,wake_group='negative',text='time-cut-'+side),part))
        for r,raw in variants:
            for variant in range(5):
                offset=40960 if not variant else int(rng.integers(33792,45057));assert offset+len(raw)+4096<=384*256
                audio=np.zeros(384*256);audio[offset:offset+len(raw)]=raw
                if variant:
                    audio*=10**(rng.uniform(-12,4)/20)
                    curve=np.array(channel['median_response_db'])*(1 if variant%2==0 else .5)+rng.uniform(-5,5,7)
                    impulse=np.roll(np.fft.irfft(10**(np.interp(freq,channel['centers_hz'],curve)/20),n=512),256)*np.hanning(512)
                    audio=np.convolve(audio,impulse,mode='same')
                    if variant>=3:
                        original=audio.copy();delay=int(rng.integers(160,960));audio[delay:]+=rng.uniform(.05,.2)*original[:-delay]
                    audio+=np.resize(noise[int(rng.integers(len(noise)))],len(audio))*rng.uniform(.2,1.5)
                audio=np.clip(np.rint(audio),-32768,32767).astype(np.int16)
                finish=offset+r['wake_end_sample'] if r['label'] else -1;ends=(np.arange(384)+1)*256;labels=np.zeros(384,dtype=np.float32)
                if finish>=0:labels[(ends>=finish)&(ends<=finish+2560)]=1
                extra.append((r,normalize(front(audio),norm),labels,finish,variant))
    n=len(extra);added={'x':np.stack([x[1] for x in extra]),'y':np.stack([x[2] for x in extra]),
        'event_end':np.array([x[3] for x in extra]),'event_start_accept':np.array([x[3]-640 if x[3]>=0 else -1 for x in extra]),
        'variant':np.array([x[4] for x in extra]),'device_domain':np.zeros(n,dtype=bool),'legacy_device_domain':np.zeros(n,dtype=bool),
        'sampling_group':np.array(['positive' if x[0]['label'] else 'paired_negative' if x[0]['text'].startswith('time-cut') else 'hard_negative' for x in extra])}
    for k in ('label','language','clip_id','source_group','text','p4_domain','new_source','wake_group'):added[k]=np.array([x[0][k] for x in extra])
    counts={}
    for split,d in datasets.items():
        select=np.ones(n,dtype=bool) if split=='train' else np.array([x[4]==0 and x[0]['tempo']==1 and x[0]['pitch']==0 for x in extra])
        assert set(d)==set(added);combined={k:np.concatenate((d[k],added[k][select])) for k in d}
        assert not any(v in str(s) for s in combined['source_group'] for v in ('bf_emma','bm_george','af_nicole','am_adam'))
        np.savez_compressed(out/(split+'.npz'),**combined);counts[split]=len(combined['x'])
    for name in ('normalization.json','channel.json'):(out/name).write_bytes((previous/name).read_bytes())
    metadata=json.loads((previous/'features.json').read_text(encoding='utf8'));metadata.update(
        joint_native_train_sources=rows,joint_native_manifest_sha256=sha(rendered/'manifest.jsonl'),
        joint_native_added_examples=n,train_examples=counts['train'],monitor_examples=counts['validation'],
        round2_audio_used_for_training=False,round3_audio_read=False,joint_data_script_sha256=sha(Path(__file__)))
    (out/'features.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps({'complete':True,'counts':counts,'added':n}))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=('render','features'));p.add_argument('--base',type=Path,required=True)
    p.add_argument('--rendered',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    if a.mode=='render':render(a.base.resolve(),a.out.resolve())
    else:features(a.base.resolve(),a.rendered.resolve(),a.out.resolve())
