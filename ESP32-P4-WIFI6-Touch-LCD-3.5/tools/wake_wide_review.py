"""Blind content review of frozen source audio, independent of KWS output."""
import argparse, concurrent.futures, hashlib, json, random, subprocess, sys, wave
from pathlib import Path
import numpy as np
from wake_prepare import ROOT, pcm

SKILL=Path.home()/'.codex/skills/qianwen-model-suite/scripts/qianwen.py'

def main():
    p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True)
    p.add_argument('--private',type=Path,default=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/.local/wide95-review')
    p.add_argument('--batch-size',type=int,choices=range(1,14),default=13)
    p.add_argument('--execute',action='store_true');a=p.parse_args();b=a.base.resolve()
    out=b/'blind-review';out.mkdir(exist_ok=True)
    plan=out/'plan.json'
    if not plan.exists():
        rows=[]
        for i in range(4):
            rows += [json.loads(s) for s in (b/f'corpus-{i}/manifest.jsonl').read_text(encoding='utf8').splitlines()]
        random.Random(11019538).shuffle(rows)
        batches=[]
        for offset in range(0,len(rows),a.batch_size):
            items=rows[offset:offset+a.batch_size];index=offset//a.batch_size;parts=[];mapping=[];position=0
            for j,row in enumerate(items):
                x=pcm(row['path']);mapping.append({'index':j+1,'clip_id':row['clip_id'],'start':position/16000,'end':(position+len(x))/16000})
                parts.extend([x,np.zeros(20000,dtype=np.int16)]);position+=len(x)+20000
            wav=out/f'batch-{index:02}.wav'
            with wave.open(str(wav),'wb') as w:
                w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(np.concatenate(parts).astype('<i2').tobytes())
            timeline=[{k:v for k,v in r.items() if k!='clip_id'} for r in mapping]
            prompt=f'按顺序独立转写这段录音中的{len(items)}段短句。每段间有1.25秒静音。不要推测用途，不要按常见名字补字。同音字无法区分时写最有把握的字并注明不确定。只返回JSON数组，每项包含index、text、language（Mandarin/Cantonese/English/uncertain）、phonemes（拼音或粤拼加数字声调，尤其最后两个中文音节）、uncertain（布尔值）。时间段：'+json.dumps(timeline,ensure_ascii=False)
            task={'capability':'omni','request':{'model':'qwen3.8-omni-flash','messages':[{'role':'user','content':[{'type':'input_audio','input_audio':{'data':{'$file':str(wav),'encoding':'data-url'},'format':'wav'}},{'type':'text','text':prompt}]}],'modalities':['text'],'stream':True,'stream_options':{'include_usage':True},'reasoning_effort':'none','max_tokens':4096}}
            taskpath=out/f'batch-{index:02}.json';taskpath.write_text(json.dumps(task,ensure_ascii=False,indent=2),encoding='utf8')
            (out/f'mapping-{index:02}.json').write_text(json.dumps(mapping,indent=2),encoding='utf8')
            batches.append({'task':str(taskpath),'wav_sha256':hashlib.sha256(wav.read_bytes()).hexdigest()})
        plan.write_text(json.dumps({'sources':len(rows),'batch_size':a.batch_size,'batches':batches,'selection_uses_kws_scores':False,'expected_text_in_prompt':False},indent=2),encoding='utf8')
    info=json.loads(plan.read_text(encoding='utf8'));print(json.dumps({'sources':info['sources'],'batches':len(info['batches'])}),flush=True)
    assert info.get('batch_size',13)==a.batch_size
    if not a.execute:return
    private=a.private.resolve();private.mkdir(exist_ok=True)
    def run(item):
        task=Path(item['task']);state=private/task.stem
        with (out/(task.stem+'.log')).open('a',encoding='utf8') as log:
            subprocess.run([sys.executable,str(SKILL),'plan','--task',str(task)],stdout=log,stderr=subprocess.STDOUT,check=True)
            result=subprocess.run([sys.executable,str(SKILL),'run','--task',str(task),'--state-dir',str(state),'--execute'],stdout=log,stderr=subprocess.STDOUT)
        print(json.dumps({'batch':task.stem,'exit':result.returncode}),flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(run,info['batches']))

if __name__=='__main__':main()
