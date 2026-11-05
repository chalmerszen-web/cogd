"""Explicitly retire failed round-one test identities and broaden TRAIN pitch.

Round-two bf_emma/bm_george identities are forbidden in this fit. Historical
round-one results stay unchanged and must never be called independent again.
Render with the existing Windows TTS environment; prepare C features in WSL.
"""
import argparse,hashlib,json,sys,wave
from pathlib import Path
import numpy as np
from wake_prepare import ROOT,pcm,bounds
from wake_realign import align
sys.path[:0]=[str(ROOT/'training/kws')]
from data import Frontend,normalize
from paired_negative import incomplete

RETIRED=['kokoro-v1-af_bella','kokoro-v1-am_fenrir','qwen3-system-Kiki']
FORBIDDEN=('bf_emma','bm_george','af_nicole','am_adam')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def render(base,out,round2=False):
    import librosa
    import soundfile as sf
    out.mkdir(parents=True,exist_ok=False)
    sources=[]
    for name in (('heldout-round2-manifest.jsonl',) if round2 else ('heldout-physical-manifest.jsonl','supplemental-native-negative-manifest.jsonl')):
        sources.extend(json.loads(s) for s in (base/name).read_text(encoding='utf8').splitlines())
    for r in sources:
        assert r['split']=='test' and r['source_group'] in RETIRED
    old=json.loads((base/'features-v5/features.json').read_text(encoding='utf8'))
    sources.extend(r for r in old['new_sources'] if not round2 and not r['p4_domain'] and r['label'] and r['wake_group'].startswith('hi')
                   and (r['split']=='train' or r['source_group'] in ('kokoro-v1-zf_xiaoxiao','kokoro-v1-zm_yunxia')))
    assert len({r['clip_id'] for r in sources})==len(sources)
    rows=[]
    for i,r in enumerate(sources):
        assert not any(v in r['source_group'] for v in FORBIDDEN)
        original=pcm(r['path']);lo,hi=bounds(original)
        for pitch in (-3,0,3):
            signal=original if not pitch else np.clip(np.rint(librosa.effects.pitch_shift(original.astype(np.float32)/32768,sr=16000,n_steps=pitch)*32768),-32768,32767).astype(np.int16)
            assert len(signal)==len(original)
            ident='expanded-'+r['clip_id']+'-pitch'+str(pitch);path=out/(ident+'.wav');sf.write(path,signal,16000,subtype='PCM_16')
            row=dict(r,clip_id=ident,path=path.relative_to(ROOT).as_posix(),split='train',previous_split=r['split'],
                     parent_clip_id=r['clip_id'],parent_wav_sha256=sha(ROOT/r['path']),pitch_semitones=pitch,
                     wake_start_sample=lo,wake_end_sample=hi,p4_domain=False,new_source=True,
                     pcm_sha256=hashlib.sha256(signal.tobytes()).hexdigest(),wav_sha256=sha(path))
            rows.append(row)
        if (i+1)%10==0:print(json.dumps({'rendered_sources':i+1,'total':len(sources)}),flush=True)
    (out/'manifest.jsonl').write_text(''.join(json.dumps(r,ensure_ascii=False)+'\n' for r in rows),encoding='utf8')
    print(json.dumps({'complete':True,'sources':len(sources),'variants':len(rows),'retired_test_groups':RETIRED}),flush=True)

def features(base,rendered,captures,out,round2=False):
    out.mkdir(parents=True,exist_ok=False);previous=base/('features-v10' if round2 else 'features-v8')
    d={s:dict(np.load(previous/(s+'.npz'),allow_pickle=False)) for s in ('train','validation')}
    norm=json.loads((previous/'normalization.json').read_text());front=Frontend();rng=np.random.default_rng(2026110109)
    rows=[json.loads(s) for s in (rendered/'manifest.jsonl').read_text(encoding='utf8').splitlines()]
    sources=[(r,pcm(r['path'])) for r in rows]
    captured=json.loads(captures.read_text(encoding='utf8'));assert captured['complete'];noise=[];ancestry=[]
    for t in captured['trials']:
        r=t['source'];assert r['split']=='train' and r['source_group'] in RETIRED
        raw=pcm(t['path']);source=pcm(r['path']);lag,quality=align(source,raw);lo,hi=bounds(source)
        start=max(0,lag-800);end=min(len(raw),lag+len(source)+1600);assert lag+hi<=len(raw)
        cut=raw[start:end]
        row=dict(r,clip_id='round1-captured-'+r['clip_id'],path=t['path'],p4_domain=True,new_source=True,
                 wake_start_sample=lag+lo-start,wake_end_sample=lag+hi-start,pitch_semitones=0)
        sources.append((row,cut));noise.append(raw[:min(4000,lag-800)].astype(float))
        ancestry.append({'parent':r['clip_id'],'capture':t['path'],'sha256':sha(ROOT/t['path']),'lag':lag,'correlation':quality,'crop':[start,end]})
    assert noise
    response=json.loads((previous/'channel.json').read_text());freq=np.fft.rfftfreq(512,1/16000)
    extra=[]
    for r,signal in sources:
        assert not any(v in r['source_group'] for v in FORBIDDEN)
        variants=[(r,signal)]
        if r['label']:
            for side in ('prefix','suffix'):
                part,_=incomplete(signal,r['wake_start_sample'],r['wake_end_sample'],side)
                variants.append((dict(r,label=0,clip_id=r['clip_id']+'-'+side,wake_group='negative',text='time-cut-'+side),part))
        for row,raw in variants:
            for variant in range(5 if row['p4_domain'] else 3):
                maximum=384*256-len(raw)-4096;assert maximum>=33280,(row['clip_id'],len(raw))
                offset=int(rng.integers(33280,min(45056,maximum)+1)) if variant else min(40960,maximum)
                audio=np.zeros(384*256,dtype=float);audio[offset:offset+len(raw)]=raw
                if variant:
                    audio*=10**(rng.uniform(-12,3)/20)
                    if not row['p4_domain']:
                        curve=(np.array(response['median_response_db']) if variant==2 else np.zeros(7))+rng.uniform(-4,4,7)
                        amp=10**(np.interp(freq,response['centers_hz'],curve)/20)
                        impulse=np.roll(np.fft.irfft(amp,n=512),256)*np.hanning(512)
                        audio=np.convolve(audio,impulse,mode='same')
                    if rng.random()<.5:
                        original=audio.copy();delay=int(rng.integers(160,800));audio[delay:]+=.15*original[:-delay]
                    audio+=np.resize(noise[int(rng.integers(len(noise)))],len(audio))*rng.uniform(.2,1)
                audio=np.clip(np.rint(audio),-32768,32767).astype(np.int16)
                finish=offset+row['wake_end_sample'] if row['label'] else -1;labels=np.zeros(384,dtype=np.float32);ends=(np.arange(384)+1)*256
                if finish>=0:labels[(ends>=finish)&(ends<=finish+2560)]=1
                extra.append((row,normalize(front(audio),norm),labels,finish,variant))
    n=len(extra);added={'x':np.stack([x[1] for x in extra]),'y':np.stack([x[2] for x in extra]),
        'event_end':np.array([x[3] for x in extra]),'event_start_accept':np.array([x[3]-640 if x[3]>=0 else -1 for x in extra]),
        'variant':np.array([x[4] for x in extra]),'device_domain':np.array([x[0]['p4_domain'] for x in extra]),
        'legacy_device_domain':np.zeros(n,dtype=bool),'sampling_group':np.array(['positive' if x[0]['label'] else 'paired_negative' if x[0]['text'].startswith('time-cut') else 'hard_negative' for x in extra])}
    for k in ('label','language','clip_id','source_group','text','p4_domain','new_source','wake_group'):added[k]=np.array([x[0][k] for x in extra])
    counts={}
    for split,data in d.items():
        chosen=np.ones(n,dtype=bool) if split=='train' else (added['variant']==0)&np.array([x[0]['pitch_semitones']==0 for x in extra])
        assert set(data)==set(added)
        combined={k:np.concatenate([data[k],added[k][chosen]]) for k in data}
        assert not any(v in str(s) for s in combined['source_group'] for v in FORBIDDEN)
        np.savez_compressed(out/(split+'.npz'),**combined);counts[split]=len(combined['x'])
    for name in ('normalization.json','channel.json'):(out/name).write_bytes((previous/name).read_bytes())
    metadata=json.loads((previous/'features.json').read_text());metadata.update(round1_test_reused_for_training=True,
        retired_test_source_groups=RETIRED,current_test_forbidden_voices=list(FORBIDDEN),
        round2_frozen_jobs_sha256=sha(base/'second-pair-test-jobs.json'),no_test_read=False,no_round2_test_read=True,
        train_examples=counts['train'],monitor_examples=counts['validation'],pitch_semitones=[-3,0,3],
        added_examples=n,new_capture_ancestry=ancestry,
        expansion_hashes={p.relative_to(ROOT).as_posix():sha(p) for p in [rendered/'manifest.jsonl',captures,Path(__file__).resolve()]})
    if round2:
        metadata.update(round2_audio_used_for_training=True,round2_test_reused_for_training=True,
            retired_round2_source_groups=RETIRED,no_round2_test_read=False,no_round3_test_read=True,
            round2_retirement_sha256=sha(base/'round2-training-retirement.json'))
    (out/'features.json').write_text(json.dumps(metadata,indent=2),encoding='utf8')
    print(json.dumps({'complete':True,'added':n,'counts':counts,'round1_retired':True,'round2_test_read':round2}),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['render','features']);p.add_argument('--base',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--rendered',type=Path);p.add_argument('--captures',type=Path);p.add_argument('--retire-round2',action='store_true');a=p.parse_args()
    if a.retire_round2:
        assert json.loads((a.base/'round2-training-retirement.json').read_text(encoding='utf8'))['round2_audio_used_for_training'] is True
        RETIRED=['kokoro-v1-bf_emma','kokoro-v1-bm_george','qwen3-system-Rocky','qwen3-system-Kiki']
        FORBIDDEN=('af_nicole','am_adam')
    if a.mode=='render':render(a.base.resolve(),a.out.resolve(),a.retire_round2)
    else:features(a.base.resolve(),a.rendered.resolve(),a.captures.resolve(),a.out.resolve(),a.retire_round2)
