"""Local-only staged synthesis; eight bundled synthetic voices, fixed splits."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import sys
import time

os.environ['HF_HUB_OFFLINE']='1'
os.environ['TRANSFORMERS_OFFLINE']='1'
os.environ['HF_HUB_DISABLE_TELEMETRY']='1'
os.environ['COGD_TTS_PRENORMALIZED']='1'

ROOT=Path(__file__).resolve().parents[2]
VOICES={'zf_xiaobei':'train','zm_yunjian':'train','zf_xiaoni':'train','zm_yunxi':'train',
        'zf_xiaoxiao':'validation','zm_yunxia':'validation','zf_xiaoyi':'test','zm_yunyang':'test'}
RESERVED_VOICES={'af_heart':'test','am_michael':'test','bf_emma':'test','bm_george':'test','af_bella':'test','am_fenrir':'test',
                 'af_nicole':'test','am_adam':'test'}
REFERENCE='今天阳光很好，我们一起出去散步，看看路边的小花。'
NEGATIVES=['你好小王','你好小严','你好小燕','你好小杨','你好小明','你好小妍','你好小叶',
           '小言','你好','嗨乐鑫','你好乐鑫','你好小智','请把灯打开','现在几点钟',
           '播放一段音乐','今天的天气不错','我们一起出去散步','等一下再说','不用回答这个问题',
           '窗外有汽车经过','请关掉电视','再见，晚安','这个声音太小了','有人在门外说话']
# Homophones (小严/小妍) are NOT fair hard negatives for acoustic KWS.
NEGATIVES=[s for s in NEGATIVES if s not in ('你好小严','你好小妍')]

def save_wave(path,samples,rate):
    import numpy as np
    import soundfile as sf
    from scipy.signal import resample_poly
    import math
    if rate!=16000:
        divisor=math.gcd(rate,16000);samples=resample_poly(samples,16000//divisor,rate//divisor)
    pcm=np.clip(np.floor(np.asarray(samples)*32768+.5),-32768,32767).astype('<i2')
    sf.write(path,pcm,16000,subtype='PCM_16')
    return pcm

def main():
    import numpy as np
    import torch
    import soundfile as sf
    ap=argparse.ArgumentParser();ap.add_argument('stage',choices=['references','yue-references','pilot','dataset'])
    ap.add_argument('--language',choices=['zh','yue'],default='zh')
    ap.add_argument('--source',type=Path,default=Path('/home/chalmers/cogd-tts-source/CosyVoice2-Yue'))
    ap.add_argument('--root',type=Path,default=ROOT/'artifacts/kws-phase2')
    ap.add_argument('--models',type=Path,help='Read-only existing model bundle; outputs still go under --root')
    ap.add_argument('--positive-count',type=int,default=50)
    ap.add_argument('--pilot-count',type=int,default=2,choices=[2,4])
    ap.add_argument('--pilot-negatives',nargs='+',choices=('你好小燕','你好','小言'),help='Optional bounded held-out pilot negatives')
    ap.add_argument('--compact-positive',action='store_true',help='Generate the complete wake phrase without a punctuation-induced middle pause')
    ap.add_argument('--positive-only',action='store_true',help='Bounded supplemental dataset; omit new negatives, preserve existing negative corpus')
    ap.add_argument('--train-count',type=int)
    ap.add_argument('--validation-count',type=int)
    ap.add_argument('--test-count',type=int)
    ap.add_argument('--variant',default='v1')
    ap.add_argument('--cantonese-reference',action='store_true')
    ap.add_argument('--carrier',action='store_true',help='Preserve a longer source sentence for subsequent ASR-aligned extraction')
    ap.add_argument('--instruct',action='store_true',help='Use instruct2 even with a Cantonese reference')
    ap.add_argument('--phonetic-alias',action='store_true',help='Yue: use the exact jyutping homophone 小賢 to stabilize the uncommon name')
    ap.add_argument('--speed',type=float,default=1.)
    ap.add_argument('--voices',nargs='+',choices=list(VOICES)+list(RESERVED_VOICES),
                    help='Explicit original or newly reserved synthetic identities; defaults remain the original eight')
    ap.add_argument('--device',choices=['cpu','cuda'],default='cuda');args=ap.parse_args()
    if args.device=='cpu':os.environ['CUDA_VISIBLE_DEVICES']=''
    args.root=args.root.resolve();args.source=args.source.resolve()
    if args.models:args.models=args.models.resolve()
    if not .8<=args.speed<=1.4:raise ValueError('Pilot speed must remain within 0.8..1.4')
    torch.set_num_threads(4)
    if args.device=='cuda' and not torch.cuda.is_available():raise RuntimeError('CUDA requested but unavailable')
    base=args.models or args.root/'tts';out=args.root/'data';out.mkdir(parents=True,exist_ok=True)
    all_voices={**VOICES,**RESERVED_VOICES}
    voices={v:all_voices[v] for v in args.voices} if args.voices else VOICES
    if args.stage=='references':
        from kokoro import KModel,KPipeline
        model=KModel(config=str(base/'references/config.json'),model=str(base/'references/kokoro-v1_0.pth'),repo_id='hexgrad/Kokoro-82M').to(args.device)
        pipeline=KPipeline(lang_code='z',model=model,repo_id='hexgrad/Kokoro-82M')
        for voice,split in voices.items():
            path=out/('reference-'+voice+'.wav')
            if path.exists():continue
            torch.manual_seed(20260921)
            parts=[r.audio.detach().cpu().numpy() for r in pipeline(REFERENCE,voice=str(base/'references/voices'/(voice+'.pt')))]
            pcm=save_wave(path,np.concatenate(parts),24000)
            print(json.dumps(dict(reference=voice,split=split,samples=len(pcm))),flush=True)
        return
    sys.path.insert(0,str(args.source));sys.path.insert(0,str(args.source/'third_party/Matcha-TTS'))
    from cosyvoice.cli.cosyvoice import CosyVoice2
    import torchaudio
    modeldir=base/('mandarin' if args.language=='zh' else 'cantonese')
    model=CosyVoice2(str(modeldir),load_jit=False,load_trt=False,load_vllm=False,fp16=args.device=='cuda')
    instruction='用普通话说这句话' if args.language=='zh' else '用粤语说这句话'
    cantonese_text='今朝天氣好好，我哋一齊出去行下，睇吓路邊啲小花。'
    manifest=out/(args.stage+'-'+args.language+('-'+args.variant if args.variant!='v1' else '')+'.jsonl')
    existing={json.loads(line)['clip_id'] for line in manifest.read_text().splitlines()} if manifest.exists() else set()
    for vindex,(voice,split) in enumerate(voices.items()):
        refprefix='reference-yue-' if args.cantonese_reference else 'reference-'
        reference,sr=sf.read(out/(refprefix+voice+'.wav'),dtype='float32')
        assert sr==16000
        prompt=torch.from_numpy(reference)[None]
        if args.stage=='yue-references':
            path=out/('reference-yue-'+voice+'.wav')
            if path.exists():continue
            torch.manual_seed(20261001+vindex)
            chunks=list(model.inference_instruct2(cantonese_text,instruction,prompt,stream=False,text_frontend=False))
            save_wave(path,torch.cat([c['tts_speech'].cpu() for c in chunks],dim=1)[0].numpy(),model.sample_rate)
            print(json.dumps(dict(cantonese_reference=voice)),flush=True)
            continue
        count=getattr(args,split+'_count') or args.positive_count
        if not 1<=count<=150:raise ValueError('Synthesis pilot cap is 150 positives per source voice')
        requests=[('你好，小言',1,i) for i in range(args.pilot_count if args.stage=='pilot' else count)]
        if args.stage=='dataset' and not args.positive_only:requests += [(text,0,i) for i,text in enumerate(NEGATIVES)]
        elif args.stage=='pilot' and args.pilot_negatives:requests += [(text,0,i) for i,text in enumerate(args.pilot_negatives)]
        for text,label,index in requests:
            ident=f'{args.stage}-{args.language}-{voice}-{label}-{index:03d}'+('-'+args.variant if args.variant!='v1' else '')
            if ident in existing:continue
            seed=20260921+vindex*10000+index*17+label*3+(0 if args.language=='zh' else 500000)
            torch.manual_seed(seed);random.seed(seed);np.random.seed(seed)
            start=time.monotonic()
            source_text=text
            if args.compact_positive and label:source_text='你好小言'
            if args.phonetic_alias and args.language=='yue' and label:
                source_text='你好小賢'
            if args.carrier:
                source_text=source_text+'。今日天氣好好。' if args.language=='yue' else source_text+'。今天的天气很好。'
            if args.cantonese_reference and not args.instruct:
                chunks=list(model.inference_zero_shot(source_text,cantonese_text,prompt,stream=False,text_frontend=False,speed=args.speed))
            else:
                chunks=list(model.inference_instruct2(source_text,instruction,prompt,stream=False,text_frontend=False,speed=args.speed))
            audio=torch.cat([c['tts_speech'].cpu() for c in chunks],dim=1)[0].numpy()
            path=out/(ident+'.wav');pcm=save_wave(path,audio,model.sample_rate)
            # Energy boundary is a reproducible approximate annotation, not forced alignment.
            energy=np.sqrt(np.mean(np.pad(pcm.astype(float),(0,(-len(pcm))%160)).reshape(-1,160)**2,axis=1))
            active=np.flatnonzero(energy>max(100,float(energy.max())*.04))
            if not len(active):raise ValueError('Silent generated clip')
            first=max(0,int(active[0])*160-320);last=min(len(pcm),(int(active[-1])+1)*160+320)
            row=dict(clip_id=ident,path=path.relative_to(ROOT).as_posix(),label=label,language=args.language,
                source_group='kokoro-v1-'+voice,reference_speaker_id='kokoro-v1-'+voice,
                original_recording_id=ident,pcm_sha256=hashlib.sha256(pcm.tobytes()).hexdigest(),
                license_id='Apache-2.0-models',split=split,text=text,source_text=source_text,seed=seed,
                reference_pcm_sha256=hashlib.sha256(reference.tobytes()).hexdigest(),
                carrier=args.carrier,needs_alignment=args.carrier,synthesis_speed=args.speed,
                compact_positive=bool(args.compact_positive and label),
                tts_family='CosyVoice2',tts_model_revision=('eec1ae6c79877dbd9379285cf8789c9e0879293d' if args.language=='zh' else 'b6d0eb0b4b594c67100e8786fe97227042d451a9'),
                reference_model_revision='f3ff3571791e39611d31c381e3a41a3af07b4987',
                wake_start_sample=first if label else None,wake_end_sample=last if label else None,
                annotation='energy_boundary_approximate',seconds=len(pcm)/16000,
                receptive_field_exceeded=bool(label and last-first>32768),generation_seconds=time.monotonic()-start)
            with manifest.open('a',encoding='utf8') as stream:stream.write(json.dumps(row,ensure_ascii=False)+'\n')
            print(json.dumps({k:row[k] for k in ('clip_id','seconds','receptive_field_exceeded','generation_seconds')}),flush=True)

if __name__=='__main__':main()
