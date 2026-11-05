"""TRAIN-only word concatenation augmentation, never an acceptance test.

Recover diverse 小言/小燕 endings from existing TRAIN voices. Independently
recognize each cropped ending, then pair it with a screened TRAIN Hi syllable.
These are explicitly constructed examples, not natural fluent utterances.
"""
import hashlib,json,sys
from pathlib import Path
import numpy as np
import soundfile as sf
import sherpa_onnx
from wake_prepare import ROOT,pcm,clean,screened,bounds
BASE=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/wake_training/20261101'

def name_ok(text,lang,label):
    s=clean(text).replace('賢','贤')
    if label:return screened({'label':1,'language':lang,'wake_group':'hi_'+lang},'hi'+s)
    return s in ({'小燕','小艳','小彦','小雁','小宴','小晏'} if lang=='zh' else {'小燕'})

def main():
    out=BASE/'train-splices';out.mkdir(exist_ok=False);base=ROOT/'artifacts/kws-phase2/asr'
    dec={l:sherpa_onnx.OfflineRecognizer.from_sense_voice(model=str(base/'model.int8.onnx'),tokens=str(base/'tokens.txt'),num_threads=4,use_itn=True,language=l) for l in ('zh','yue')}
    def asr(signal,lang):
        s=dec[lang].create_stream();s.accept_waveform(16000,np.r_[signal.astype(np.float32)/32768,np.zeros(8000,dtype=np.float32)]);dec[lang].decode_stream(s);return s.result
    old=list(map(json.loads,(ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl').read_text(encoding='utf8').splitlines()))
    new=list(map(json.loads,(BASE/'combined-manifest.jsonl').read_text(encoding='utf8').splitlines()))
    audit={r['clip_id']:r for r in json.loads((BASE/'combined-asr-padded.json').read_text(encoding='utf8'))['clips']}
    prefixes={l:[] for l in dec}
    for r in new:
        if r['split']=='train' and r['text']=='Hi' and clean(audit[r['clip_id']]['asr_text']) in ('hi','嗨','hai'):
            signal=pcm(r['path']);lo,hi=bounds(signal);prefixes[r['language']].append((r,signal[lo:hi]))
    assert all(prefixes.values())
    sources=[]
    for lang in dec:
        voices=sorted({r['source_group'] for r in old if r['split']=='train' and r['language']==lang and r['label']})
        for voice in voices:
            for label in (1,0):
                candidates=[r for r in old if r['split']=='train' and r['language']==lang and r['source_group']==voice
                            and (r['label']==1 if label else r.get('text')=='你好小燕')]
                candidates.sort(key=lambda r:r['clip_id'])
                if len(candidates)>8:candidates=[candidates[i] for i in np.linspace(0,len(candidates)-1,8).astype(int)]
                sources += [(r,label) for r in candidates]
    notes=[];rows=[];rng=np.random.default_rng(2026110104)
    for r,label in sources:
        signal=pcm(r['path']);lang=r['language'];result=asr(signal,lang);note={'source':r['clip_id'],'label':label,'full_text':result.text,'accepted':False}
        full=clean(result.text).replace('賢','贤')
        if full.startswith('你好') and name_ok(full[2:],lang,label):
            chars=[(c,float(t)) for token,t in zip(result.tokens,result.timestamps) for c in clean(token)]
            little=next((t for c,t in chars if c=='小'),None)
            if little is not None:
                start=max(0,int((little-.12)*16000));ending=signal[start:]
                cropped=asr(ending,lang);note['crop_text']=cropped.text;note['crop_start']=start
                if name_ok(cropped.text,lang,label):
                    try:lo,hi=bounds(ending)
                    except ValueError:lo=hi=0
                    if hi>lo:
                        ending=ending[lo:hi];note['accepted']=True
                        for variant,gap in enumerate((0,1280)):
                            parent,prefix=prefixes[lang][int(rng.integers(len(prefixes[lang])))]
                            def level(x):
                                y=x.astype(float);return y*(2600/max(1,np.sqrt(np.mean(y*y))))
                            left=level(prefix);right=level(ending)
                            fade=min(80,len(left),len(right));left[-fade:]*=np.linspace(1,0,fade);right[:fade]*=np.linspace(0,1,fade)
                            raw=np.clip(np.rint(np.r_[left,np.zeros(gap),right]),-32768,32767).astype('<i2')
                            if len(raw)>32768:continue
                            ident=f'p4-splice-{lang}-{len(rows):04d}';path=out/(ident+'.wav');sf.write(path,raw,16000,subtype='PCM_16')
                            rows.append(dict(clip_id=ident,path=path.relative_to(ROOT).as_posix(),label=label,language=lang,
                                wake_group='hi_'+lang if label else 'negative',text='Hi，小言' if label else 'Hi，小燕',split='train',
                                source_group=r['source_group'],parent_source_groups=[parent['source_group'],r['source_group']],
                                original_recording_id=ident,parent_clip_ids=[parent['clip_id'],r['clip_id']],
                                component_asr=[audit[parent['clip_id']]['asr_text'],cropped.text],constructed=True,
                                source_parent_paths=[parent['path'],r['path']],license_id='Apache-2.0-models',
                                pcm_sha256=hashlib.sha256(raw.tobytes()).hexdigest(),wav_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                                wake_start_sample=0,wake_end_sample=len(raw),seconds=len(raw)/16000,
                                annotation='TRAIN-only concatenation of individually ASR-screened Hi and name; not fluent speech or test evidence'))
        notes.append(note)
        if len(notes)%20==0:print(json.dumps({'screened':len(notes),'sources':len(sources),'constructed':len(rows)}),flush=True)
    (out/'manifest.jsonl').write_text(''.join(json.dumps(r,ensure_ascii=False)+'\n' for r in rows),encoding='utf8')
    (out/'component-audit.json').write_text(json.dumps({'complete':True,'clips':notes,'constructed':len(rows),'all_train':True},ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({'complete':True,'constructed':len(rows)}),flush=True)

if __name__=='__main__':main()
