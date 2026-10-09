"""Create a finite, receipted synthetic conversation corpus; never retry paid POSTs."""
import argparse
import json
from pathlib import Path
import time
import wave
from cloud_probe import HTTP,platform_key,ROOT,validate_task

PROMPTS=[
    ('blue','请把四颗灯设为蓝色，请简单回答。',['蓝']),
    ('recall_blue','刚才我让你把灯改成了什么颜色？',['颜色']),
    ('remember','请记住，我给这盏灯取名叫小星星。',['星']),
    ('recall_name','我刚给这盏灯取的名字是什么？',['名字']),
    ('green','请把四颗灯改成绿色，请简单回答。',['绿']),
    ('greeting','请用一句话介绍你自己。',['介绍']),
]


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    key=platform_key(Path.home()/'Desktop/key.txt');http=HTTP(key)
    raw=json.loads((ROOT/'artifacts/voice-cloud/voices.json').read_text(encoding='utf8'))
    voices=raw.get('data',raw.get('voices',[])) if isinstance(raw,dict) else raw
    voices=[v['id'] for v in voices if v.get('status')=='ready'][:2]
    report={'complete':False,'synthetic':True,'prompts':[]}
    def save(): (a.out/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    save()
    for i,(name,text,required) in enumerate(PROMPTS):
        row=dict(id=name,text=text,required=required,voice=voices[i%len(voices)],submitting=True,started=time.time())
        task=validate_task({'operation':'speech','voice':row['voice'],'input':text,'response_format':'wav','speed':1.0,'output':name+'.wav'},a.out.resolve())
        report['prompts'].append(row);save()
        target=a.out/(name+'-original.wav')
        http.download('/v1/audio/speech',target,authenticated=True,method='POST',payload={
            'model':'openai-tts',**{k:task[k] for k in ('voice','input','response_format','speed')}})
        with wave.open(str(target),'rb') as source:
            assert source.getsampwidth()==2 and source.getnchannels()==1
            rate=source.getframerate();pcm=source.readframes(rate*12)
            assert not source.readframes(1),'Synthetic utterance exceeds capture budget'
        with wave.open(str(a.out/(name+'.wav')),'wb') as dest:
            dest.setparams((1,2,rate,0,'NONE','not compressed'));dest.writeframes(pcm)
        row.update(complete=True,path=str(a.out/(name+'.wav')),seconds=len(pcm)/(2*rate),finished=time.time());save()
        print(name+' generated',flush=True)
    report['complete']=True;save()


if __name__=='__main__':main()
