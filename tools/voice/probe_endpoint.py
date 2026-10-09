"""Exactly four host-only server-VAD sessions, no retries or hardware."""
import argparse,base64,hashlib,json,queue,threading,time,wave
from pathlib import Path
import numpy as np
from scipy import signal
import websocket
from qianwen_credentials import qianwen_key
MODEL='qwen-audio-3.0-realtime-flash'
URL='wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model='+MODEL

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--profile',choices=['original','followup'],default='original');parser.add_argument('--output',type=Path,default=Path('artifacts/voice-fast/endpoint-probe-01'));args=parser.parse_args()
 out=args.output;out.mkdir(parents=True,exist_ok=False)
 source=Path('artifacts/voice-fast/warm-noise-clip/device-clip.wav')
 with wave.open(str(source),'rb') as w:
  assert w.getframerate()==16000 and w.getnchannels()==1 and w.getsampwidth()==2
  pcm=w.readframes(w.getnframes())
 assert len(pcm)==320000
 report=dict(model=MODEL,url=URL,source=str(source),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),source_active_end_estimate_s=3.78,retries=0,trials=[])
 def save():(out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
 save();key=qianwen_key()
 settings=[(.5,300,False),(.1,300,False),(.1,500,False),(.1,300,True)] if args.profile=='original' else [(None,None,False),(.1,800,False)]
 for index,(threshold,silence,filtered) in enumerate(settings):
  data=pcm;r=dict(index=index,threshold=threshold,silence_ms=silence,filter=None,events=[],transcript=None,speech_started=None,speech_stopped=None,sent_samples=0)
  if filtered:
   sos=signal.butter(2,180,btype='highpass',fs=16000,output='sos');x=signal.sosfilt(sos,np.frombuffer(pcm,dtype='<i2').astype(float));data=np.clip(np.rint(x),-32768,32767).astype('<i2').tobytes();r['filter']=dict(type='causal Butterworth highpass',order=2,hz=180,initial_state='zero',sos=sos.tolist())
  target=out/f'input-{index}.pcm';target.write_bytes(data);r['input_pcm_sha256']=hashlib.sha256(data).hexdigest();report['trials'].append(r);save()
  start=time.monotonic();deadline=start+20;ws=None;stop=threading.Event();incoming=queue.Queue()
  def receive(sock,stopped,q):
   try:
    while not stopped.is_set():
     raw=sock.recv()
     if not raw:break
     q.put((time.monotonic(),json.loads(raw)))
   except Exception as exc:q.put((time.monotonic(),{'type':'local_receive_error','error_type':type(exc).__name__}))
  try:
   ws=websocket.create_connection(URL,header=['Authorization: Bearer '+key],timeout=max(.1,deadline-time.monotonic()),redirect_limit=0,enable_multithread=True)
   threading.Thread(target=receive,args=(ws,stop,incoming),daemon=True).start();ready=False;began=None;offset=0
   config=dict(modalities=['text','audio'],voice='longanqian',input_audio_format='pcm',output_audio_format='pcm',enable_search=False,tools=[],instructions='简短中文回答，不执行任何设备操作。',turn_detection=(dict(type='smart_turn') if threshold is None else dict(type='server_vad',threshold=threshold,silence_duration_ms=silence)));r['config']=config
   while time.monotonic()<deadline:
    try:
     at,e=incoming.get(timeout=.005);kind=e.get('type')
     e=json.loads(json.dumps(e,ensure_ascii=False).replace(key,'[REDACTED]'))
     if kind=='response.audio.delta':e={k:v for k,v in e.items() if k!='delta'}
     r['events'].append(dict(observed_s=at-start,event=e))
     if kind=='session.created':ws.send(json.dumps(dict(type='session.update',session=config)))
     if kind=='session.updated':ready=True;began=time.monotonic()
     if kind=='input_audio_buffer.speech_started':r['speech_started']=e
     if kind=='input_audio_buffer.speech_stopped':
      if threshold is None and e.get('reason')=='turn_invalid':r.setdefault('invalid_turns',[]).append(e)
      else:r['speech_stopped']=e
     if kind=='conversation.item.input_audio_transcription.completed':r['transcript']=e.get('transcript')
     if kind=='response.done':r['response_done']=True
     if kind in ('error','local_receive_error'):r['error']=e;break
     save()
    except queue.Empty:pass
    if ready and not r['speech_stopped'] and offset<len(data) and time.monotonic()>=began+(offset+640)/32000:
     chunk=data[offset:offset+640];ws.settimeout(max(.1,deadline-time.monotonic()));ws.send(json.dumps(dict(type='input_audio_buffer.append',audio=base64.b64encode(chunk).decode())));offset+=len(chunk);r['sent_samples']=offset//2
    if r['speech_stopped'] and r['transcript'] is not None and r.get('response_done'):break
   r['deadline_reached']=time.monotonic()>=deadline
  except Exception as exc:r['error_type']=type(exc).__name__
  finally:
   stop.set()
   if ws:ws.close(timeout=.1)
   r['elapsed_s']=time.monotonic()-start;save()
  print(json.dumps({k:r.get(k) for k in ['index','sent_samples','speech_started','speech_stopped','transcript','deadline_reached','error_type']},ensure_ascii=False),flush=True)
 report['complete']=True;save()
if __name__=='__main__':main()
