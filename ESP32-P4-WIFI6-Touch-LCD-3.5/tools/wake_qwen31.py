"""Bounded, speaker-disjoint Qwen 3.1 synthetic corpus; one POST per source."""
import argparse, hashlib, json, subprocess, sys
from pathlib import Path
import soundfile as sf
from wake_native_yue import recover
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/tts'))
from generate import save_wave
SKILL=Path.home()/'.codex/skills/qianwen-model-suite/scripts/qianwen.py'

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def jobs():
    result=[]
    for split,voices in [('train',('longanhuan_v3.1','longanlingxin_v3.1')),('test',('longanfengyue_v3.1','xunanchuan_v3.1'))]:
        for voice in voices:
            for lang in ('zh','yue'):
                positive=[('你好，小言。','nihao_'+lang),('Hi，小言。','hi_'+lang)]
                specs=[(text,g,True,rate) for rate in ((.85,1.,1.15,.95) if split=='train' else (.92,1.08)) for text,g in positive]
                negatives=['你好，小燕。','Hi，小燕。','你好，小杨。','Hi，小杨。','你好。','Hi。','小言。','请把毛巾递给我。']
                if split=='test':negatives=['Hi，小燕。','你好，小燕。','Hi，小杨。','Hi。','小言。','请把毛巾递给我。']
                specs += [(text,'negative',False,1.) for text in negatives]
                for i,(text,group,label,rate) in enumerate(specs):
                    ident=f'q31-{split}-{voice}-{lang}-{i:02}'
                    inp={'text':text,'voice':voice,'format':'wav','sample_rate':24000,'rate':rate,'seed':1101+len(result),
                         'instruction':'请用自然的普通话朗读。' if lang=='zh' else '請用自然的廣東話朗讀。'}
                    if lang=='zh':
                        inp['hot_fix']={'pronunciation':[{n:ph} for n,ph in [('小言','xiao3 yan2'),('小燕','xiao3 yan4'),('小杨','xiao3 yang2')] if n in text]}
                    result.append(dict(clip_id=ident,split=split,voice=voice,source_group='qwen31-system-'+voice,
                        reference_speaker_id='qwen31-system-'+voice,language=lang,label=int(label),wake_group=group,
                        text=text.rstrip('。'),request={'capability':'tts','request':{'model':'qwen-audio-3.1-tts-flash','input':inp}}))
    return result

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--private',type=Path,required=True)
    p.add_argument('--jobs',type=Path,help='Explicit frozen source plan for a separately documented TTS API')
    p.add_argument('--execute',action='store_true');a=p.parse_args();a.out=a.out.resolve();a.private=a.private.resolve()
    work=json.loads(a.jobs.read_text(encoding='utf8')) if a.jobs else jobs()
    train_voices={r['source_group'] for r in work if r['split']=='train'};test_voices={r['source_group'] for r in work if r['split']=='test'}
    plan={'models':sorted({r['request']['request']['model'] for r in work}),'count':len(work),'train':sum(r['split']=='train' for r in work),
                     'test':sum(r['split']=='test' for r in work),'speaker_disjoint':not bool(train_voices&test_voices),'human_recordings':False}
    a.out.mkdir(parents=True,exist_ok=True);frozen=a.out/'jobs.json';content=json.dumps(work,ensure_ascii=False,indent=2)
    if frozen.exists():assert frozen.read_text(encoding='utf8')==content
    else:frozen.write_text(content,encoding='utf8')
    (a.out/'plan.json').write_text(json.dumps(plan,indent=2),encoding='utf8');print(json.dumps(plan),flush=True)
    if not a.execute:return
    a.private.mkdir(parents=True,exist_ok=True);manifest=a.out/'manifest.jsonl'
    done={r['clip_id'] for r in map(json.loads,manifest.read_text(encoding='utf8').splitlines())} if manifest.exists() else set()
    for row in work:
        ident=row['clip_id']
        if ident in done:continue
        task=a.private/(ident+'.json');state=a.private/ident;raw=a.private/(ident+'.wav')
        if not task.exists():task.write_text(json.dumps(row['request'],ensure_ascii=False,indent=2),encoding='utf8')
        with (a.private/(ident+'.log')).open('a',encoding='utf8') as log:
            subprocess.run([sys.executable,str(SKILL),'plan','--task',str(task)],stdout=log,stderr=subprocess.STDOUT,check=True)
            subprocess.run([sys.executable,str(SKILL),'run','--task',str(task),'--state-dir',str(state),'--execute'],stdout=log,stderr=subprocess.STDOUT,check=False)
        receipt=json.loads((state/'state.json').read_text(encoding='utf8'))
        if receipt['status']!='succeeded':
            with (a.out/'unavailable.jsonl').open('a',encoding='utf8') as f:f.write(json.dumps({'id':ident,'status':receipt['status']})+'\n')
            print(json.dumps({'id':ident,'status':receipt['status']}),flush=True);continue
        if not raw.exists():recover(state,raw)
        samples,rate=sf.read(raw,dtype='float32');path=a.out/(ident+'.wav');pcm=save_wave(path,samples,rate)
        record={k:v for k,v in row.items() if k!='request'}
        record.update(path=path.relative_to(ROOT).as_posix(),seconds=len(pcm)/16000,wake_start_sample=0,wake_end_sample=len(pcm),
                      original_recording_id=ident,pcm_sha256=hashlib.sha256(pcm.tobytes()).hexdigest(),wav_sha256=sha(path),
                      request_id=receipt['request_id'],license_id='Qwen-API-generated-audio',tts_family=row['request']['request']['model'],
                      annotation='Synthetic; explicit source parameters retained in jobs.json; independent content audit required')
        with manifest.open('a',encoding='utf8') as f:f.write(json.dumps(record,ensure_ascii=False)+'\n')
        print(json.dumps({'id':ident,'seconds':record['seconds'],'saved':True}),flush=True)

if __name__=='__main__':main()
