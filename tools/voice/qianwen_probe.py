"""One bounded paid WSS task; reruns never create another task in the same directory."""
import argparse
import hashlib
import json
from pathlib import Path
import threading
import time
import uuid
import wave
import websocket
from qianwen_credentials import qianwen_key


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mode',choices=['asr','tts'],required=True)
    p.add_argument('--input',type=Path)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    if a.out.exists(): raise RuntimeError('Existing task preserved; no resubmission')
    pcm=b''
    if a.mode=='asr':
        with wave.open(str(a.input),'rb') as w:
            assert w.getsampwidth()==2 and w.getnchannels()==1 and w.getframerate()==16000
            pcm=w.readframes(w.getnframes())
        assert 0<len(pcm)<=320000
    key=qianwen_key();task_id=uuid.uuid4().hex
    a.out.mkdir(parents=True)
    start=time.monotonic()
    state={'complete':False,'mode':a.mode,'task_id':task_id,'starts':1,'events':[],
           'input_sha256':hashlib.sha256(pcm).hexdigest() if pcm else None}
    def save(): (a.out/'report.json').write_text(json.dumps(state,ensure_ascii=False,indent=2),encoding='utf8')
    def stamp(): return round(time.monotonic()-start,4)
    save();ws=None;sender=None;stop=threading.Event();failures=[];audio=bytearray()
    def send(action,payload):
        ws.send(json.dumps({'header':{'action':action,'task_id':task_id,'streaming':'duplex'},'payload':payload},ensure_ascii=False))
    try:
        ws=websocket.create_connection('wss://dashscope.aliyuncs.com/api-ws/v1/inference/',
            header=['Authorization: Bearer '+key],timeout=30,redirect_limit=0)
        state['connected_s']=stamp()
        if a.mode=='asr':
            payload={'task_group':'audio','task':'asr','function':'recognition','model':'fun-asr-realtime',
                     'parameters':{'format':'pcm','sample_rate':16000},'input':{}}
        else:
            payload={'task_group':'audio','task':'tts','function':'SpeechSynthesizer','model':'qwen-audio-3.0-tts-flash',
                     'parameters':{'text_type':'PlainText','voice':'longanhuan_v3.6','format':'pcm','sample_rate':24000},'input':{}}
        state['request']=payload;send('run-task',payload)
        def produce():
            try:
                if a.mode=='asr':
                    for offset in range(0,len(pcm),3200):
                        if stop.is_set(): return
                        ws.send_binary(pcm[offset:offset+3200]);time.sleep(len(pcm[offset:offset+3200])/32000)
                else:
                    send('continue-task',{'input':{'text':'你好，我是小言。'}})
                    time.sleep(.2)
                    send('continue-task',{'input':{'text':'现在使用实时语音，请把四颗灯设为蓝色。'}})
                state['finish_sent_s']=stamp();send('finish-task',{'input':{}})
            except Exception as e: failures.append(type(e).__name__);stop.set()
        offset=0;next_send=0;started=False;finished=False
        while stamp()<45:
            if a.mode=='asr' and started:
                if offset<len(pcm) and time.monotonic()>=next_send:
                    chunk=pcm[offset:offset+3200];ws.send_binary(chunk);offset+=len(chunk)
                    next_send=time.monotonic()+len(chunk)/32000
                elif offset==len(pcm) and not finished and time.monotonic()>=next_send:
                    state['finish_sent_s']=stamp();send('finish-task',{'input':{}});finished=True
            ws.settimeout(.03 if a.mode=='asr' and started else 30)
            try:opcode,packet=ws.recv_data(control_frame=True)
            except websocket.WebSocketTimeoutException:continue
            if opcode==websocket.ABNF.OPCODE_CLOSE:
                state['close_code']=int.from_bytes(packet[:2],'big') if len(packet)>=2 else None
                state['close_reason']=packet[2:].decode('utf8','replace').replace(key,'[redacted]')
                break
            if opcode in (websocket.ABNF.OPCODE_PING,websocket.ABNF.OPCODE_PONG):continue
            if opcode==websocket.ABNF.OPCODE_BINARY:
                if not audio:state['first_pcm_s']=stamp()
                audio.extend(packet);assert len(audio)<=24000*2*25
                continue
            event=json.loads(packet)
            assert event.get('header',{}).get('task_id')==task_id
            event['observed_s']=stamp();state['events'].append(event);save()
            kind=event['header']['event']
            if kind=='task-started':
                assert not started;started=True
                if a.mode=='tts':produce()
            if kind=='task-failed':break
            if kind=='task-finished':state['complete']=True;break
            if failures:break
    except Exception as e:
        # Never render exception text: network libraries can include request headers.
        state['exception_type']=type(e).__name__
        state['exception_message']=str(e).replace(key,'[redacted]')[:300]
    finally:
        stop.set()
        if sender:sender.join(2)
        if ws:ws.close()
        if audio:
            with wave.open(str(a.out/'result.wav'),'wb') as w:
                w.setparams((1,2,24000,0,'NONE','not compressed'));w.writeframes(audio)
            state['audio_seconds']=len(audio)/48000
        state['finished_s']=stamp();state['sender_failures']=failures;save()
    print(json.dumps({k:v for k,v in state.items() if k not in ('events','request')},ensure_ascii=False))
    if not state['complete']:raise SystemExit(1)


if __name__=='__main__':main()
