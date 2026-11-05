"""Frozen, source-disjoint local TTS for the four P4 wake pronunciations.

Different seeds are variants, not independent speakers. Outputs require ASR
screening; these approximate energy endpoints are not phonetic ground truth.
"""
import argparse, gc, hashlib, json, os, random, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/wake_training/20261101'
sys.path.insert(0, str(ROOT/'training/tts'))

def freeze():
    ancestry = [json.loads(s) for s in (ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl').read_text(encoding='utf8').splitlines()]
    voices = {'train':['zf_xiaobei','zf_xiaoni','zf_xiaoyi','zm_yunjian','zm_yunxi','zm_yunyang'],
              'validation':['zf_xiaoxiao','zm_yunxia'], 'test':['af_bella','am_fenrir']}
    jobs = []
    for language in ('zh','yue'):
        for split, names in voices.items():
            for voice in names:
                group = 'kokoro-v1-'+voice
                assert {r['split'] for r in ancestry if r['source_group']==group} == {split}
                for word in ('nihao','hi'):
                    count = (2 if word=='nihao' else 4) if split=='train' else (2 if split=='validation' else 5)
                    for i in range(count):
                        name = '小言' if language=='zh' else '小賢'
                        prefix = '你好' if word=='nihao' else ('Hi' if i%2==0 else '嗨')
                        source = prefix+('，' if i%3==1 else '')+name
                        jobs.append(dict(language=language,split=split,voice=voice,source_group=group,label=1,
                            wake_group=word+'_'+language,text=('你好' if word=='nihao' else 'Hi')+'，小言',
                            source_text=source,speed=[1.,.85,1.15,1.05,.95][i%5]))
                for text in ('Hi','小言','Hi，小燕','Hi，小杨','Hi，小王'):
                    jobs.append(dict(language=language,split=split,voice=voice,source_group=group,label=0,
                        wake_group='negative',text=text,source_text=text,speed=1.))
    for i, row in enumerate(jobs):
        row.update(clip_id=f'p4-wake4-{i:04d}',seed=2026110100+i*17)
    path=BASE/'synthesis-jobs.json'
    value=json.dumps(jobs,ensure_ascii=False,indent=2)+'\n'
    if path.exists(): assert path.read_text(encoding='utf8')==value
    else: path.write_text(value,encoding='utf8')
    return jobs

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--limit',type=int);ap.add_argument('--carrier-yue',action='store_true')
    ap.add_argument('--hi-yue-only',action='store_true',help='Reuse existing nihao Yue corpus; add Hi and three near/partial negatives')
    ap.add_argument('--test-hi-zh-alias',action='store_true',help='Additional reserved test voices using the exact Mandarin homophone 小妍')
    ap.add_argument('--test-second-pair',action='store_true',help='New reserved bf_emma/bm_george voices; round-one test becomes development')
    ap.add_argument('--test-third-pair',action='store_true',help='New reserved af_nicole/am_adam voices; round-two becomes development')
    ap.add_argument('--only-language',choices=['zh','yue'])
    a=ap.parse_args()
    jobs=freeze()
    assert not (a.test_second_pair and a.test_third_pair)
    test_round=3 if a.test_third_pair else 2 if a.test_second_pair else 0
    if test_round:
        assert not a.test_hi_zh_alias
        mapping={'af_bella':'af_nicole','am_fenrir':'am_adam'} if test_round==3 else {'af_bella':'bf_emma','am_fenrir':'bm_george'}
        jobs=[dict(r,clip_id=r['clip_id'].replace('p4-wake4-',f'p4-wake4-r{test_round}-'),voice=mapping[r['voice']],
                   source_group='kokoro-v1-'+mapping[r['voice']],seed=r['seed']+(380041 if test_round==3 else 190019))
              for r in jobs if r['split']=='test']
        frozen=BASE/('third-pair-test-jobs.json' if test_round==3 else 'second-pair-test-jobs.json');content=json.dumps(jobs,ensure_ascii=False,indent=2)+'\n'
        if frozen.exists():assert frozen.read_text(encoding='utf8')==content
        else:frozen.write_text(content,encoding='utf8')
    if a.test_hi_zh_alias:
        assert not a.carrier_yue and not a.hi_yue_only
        jobs=[dict(r,clip_id=r['clip_id']+'-alias1',source_text=('Hi' if i%2==0 else '嗨')+'，小妍',seed=r['seed']+7077)
              for i,r in enumerate(jobs) if r['split']=='test' and r['wake_group']=='hi_zh']
        frozen=BASE/'extra-zh-test-jobs.json';content=json.dumps(jobs,ensure_ascii=False,indent=2)+'\n'
        if frozen.exists():assert frozen.read_text(encoding='utf8')==content
        else:frozen.write_text(content,encoding='utf8')
    if a.carrier_yue:
        jobs=[dict(r,clip_id=r['clip_id']+'-carrier1',source_text=r['source_text']+'。今日天氣好好。',carrier=True) for r in jobs if r['language']=='yue']
        frozen=BASE/(f'round{test_round}-carrier-jobs.json' if test_round else 'carrier-jobs.json');content=json.dumps(jobs,ensure_ascii=False,indent=2)+'\n'
        if frozen.exists():assert frozen.read_text(encoding='utf8')==content
        else:frozen.write_text(content,encoding='utf8')
    out=BASE/((f'round{test_round}-carrier' if a.carrier_yue else f'round{test_round}-synthetic') if test_round else 'synthetic-extra-zh' if a.test_hi_zh_alias else 'synthetic-carrier' if a.carrier_yue else 'synthetic');out.mkdir(exist_ok=True)
    for key in ('HF_HUB_OFFLINE','TRANSFORMERS_OFFLINE','HF_HUB_DISABLE_TELEMETRY','COGD_TTS_PRENORMALIZED'):
        os.environ[key]='1'
    import numpy as np
    import soundfile as sf
    import torch
    from generate import save_wave
    source=ROOT/'.local/tts-source/CosyVoice2-Yue'
    sys.path[:0]=[str(source),str(source/'third_party/Matcha-TTS')]
    from cosyvoice.cli.cosyvoice import CosyVoice2
    torch.set_num_threads(4)
    assert torch.cuda.is_available(), 'Use the existing Windows CUDA environment'
    manifest=out/'manifest.jsonl'
    done={r['clip_id'] for r in map(json.loads,manifest.read_text(encoding='utf8').splitlines())} if manifest.exists() else set()
    if a.hi_yue_only:
        assert a.carrier_yue
        jobs=[r for r in jobs if r['clip_id'] in done or r['wake_group']=='hi_yue' or (not r['label'] and r['text'] in ('Hi','小言','Hi，小燕'))]
        (BASE/'carrier-execution-selection.json').write_text(json.dumps(jobs,ensure_ascii=False,indent=2),encoding='utf8')
    selected=[r for r in jobs if r['clip_id'] not in done and (not a.only_language or r['language']==a.only_language)]
    if a.limit: selected=selected[:a.limit]
    began=time.monotonic()
    for language in ('zh','yue'):
        requests=[r for r in selected if r['language']==language]
        if not requests: continue
        model=CosyVoice2(str(ROOT/'artifacts/kws-phase2/tts'/('mandarin' if language=='zh' else 'cantonese')),
                        load_jit=False,load_trt=False,load_vllm=False,fp16=True)
        for r in requests:
            if time.monotonic()-began>10800: raise TimeoutError('Three-hour finite synthesis budget')
            start=time.monotonic()
            ref=ROOT/'artifacts/kws-phase2/data'/('reference-'+('yue-' if language=='yue' else '')+r['voice']+'.wav')
            samples, rate=sf.read(ref,dtype='float32');assert rate==16000 and samples.ndim==1
            prompt=torch.from_numpy(samples)[None]
            torch.manual_seed(r['seed']);random.seed(r['seed']);np.random.seed(r['seed'])
            if language=='yue':
                chunks=list(model.inference_zero_shot(r['source_text'],'今朝天氣好好，我哋一齊出去行下，睇吓路邊啲小花。',prompt,
                    stream=False,text_frontend=False,speed=r['speed']))
            else:
                chunks=list(model.inference_instruct2(r['source_text'],'用普通话说这句话',prompt,
                    stream=False,text_frontend=False,speed=r['speed']))
            audio=torch.cat([c['tts_speech'].cpu() for c in chunks],dim=1)[0].numpy()
            path=out/(r['clip_id']+'.wav');pcm=save_wave(path,audio,model.sample_rate)
            rms=np.sqrt(np.mean(np.pad(pcm.astype(float),(0,(-len(pcm))%160)).reshape(-1,160)**2,axis=1))
            active=np.flatnonzero(rms>=max(64,rms.max()*.025))
            lo=max(0,(int(active[0])-2)*160) if len(active) else 0
            hi=min(len(pcm),(int(active[-1])+3)*160) if len(active) else len(pcm)
            row=dict(r,path=path.relative_to(ROOT).as_posix(),seconds=len(pcm)/16000,
                original_recording_id=r['clip_id'],reference_speaker_id=r['source_group'],
                reference_pcm_sha256=hashlib.sha256(samples.tobytes()).hexdigest(),
                pcm_sha256=hashlib.sha256(pcm.tobytes()).hexdigest(),wav_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                wake_start_sample=lo,wake_end_sample=hi,license_id='Apache-2.0-models',
                annotation='2.5 percent peak 10ms energy, 20ms margins; automatic only',
                tts_family='CosyVoice2',tts_revision='eec1ae6c79877dbd9379285cf8789c9e0879293d' if language=='zh' else 'b6d0eb0b4b594c67100e8786fe97227042d451a9',
                generation_seconds=time.monotonic()-start)
            with manifest.open('a',encoding='utf8') as f: f.write(json.dumps(row,ensure_ascii=False)+'\n')
            done.add(r['clip_id']);print(json.dumps(dict(done=len(done),total=len(jobs),id=r['clip_id'],seconds=row['seconds'],elapsed=row['generation_seconds'])),flush=True)
        del model,chunks,prompt
        gc.collect();torch.cuda.empty_cache()
    (out/'generation-status.json').write_text(json.dumps(dict(complete=len(done)==len(jobs),generated=len(done),total=len(jobs),seconds=time.monotonic()-began),indent=2),encoding='utf8')

if __name__=='__main__': main()
