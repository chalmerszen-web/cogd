"""One file-only session: early preparation tool followed by verified final ASR.

No physical tools, recording, playback, retry or reconnect. The preparation
function reads the real committed transcription; RGB calls are proposals only.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import queue
import subprocess
import threading
import time
import wave

from input_integrity import complete_input

MODEL = 'qwen3.5-omni-flash-realtime'
PREP = ('你是小言。用户可能还没说完或会改口。每个新语音回合先调用一次'
        'await_final_input，等待完整指令。调用之前不要回答、不执行其他操作。'
        '这个函数返回带输入编号的完整转写，它是本轮最终意图的依据。')
ANSWER = ('你是小言，ESP-HI设备语音助手。依据本轮await_final_input返回的完整转写'
          '决定回复，后半句改口优先，不沿用先前猜测。只回复一句，最多16字。'
          '灯色请求调用device_light_set_rgb；取得成功结果前不能声称已完成。'
          '无需工具的介绍问题直接介绍自己。不要提内部转写、草稿或函数名。')
WAIT_TOOL = dict(type='function', function=dict(name='await_final_input',
    description='只读等待本轮语音完整转写，返回最终输入编号和内容，不控制硬件。',
    parameters=dict(type='object', properties={}, additionalProperties=False)))
RGB_TOOL = dict(type='function', function=dict(name='device_light_set_rgb',
    description='设置四颗灯的RGB颜色，只有工具成功后才可告知完成。',
    parameters=dict(type='object', properties={c:dict(type='integer',minimum=0,maximum=255)
                                              for c in 'rgb'},required=list('rgb'),additionalProperties=False)))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--execute',action='store_true')
    args=p.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    manifests=[Path('artifacts/voice-cloud/prompts-01/manifest.json'),
               Path('artifacts/voice-fast/prefetch-fixtures-01/manifest.json')]
    rows={}
    for path in manifests:
        m=json.loads(path.read_text(encoding='utf-8-sig'));assert m['synthetic'] and m['complete']
        rows.update({r['id']:r for r in m['prompts']})
    fixtures=[]
    for name in ('greeting','blue','correction'):
        r=rows[name];source=Path(r['path'])
        pcm=subprocess.run(['ffmpeg','-v','error','-i',str(source),'-ar','16000','-ac','1',
                            '-f','s16le','-'],check=True,capture_output=True,timeout=15).stdout
        assert 1.5*32000<len(pcm)<=8*32000 and not len(pcm)%2
        fixtures.append((r,pcm))
    config=dict(modalities=['text','audio'],voice='Tina',input_audio_format='pcm',
        audio=dict(output=dict(format=dict(type='pcm',sample_rate=16000))),
        turn_detection=None,enable_search=False,max_tokens=384,instructions=PREP,tools=[WAIT_TOOL])
    report=dict(complete=False,accepted=False,execute=args.execute,attempts=0,retries=0,
        model=MODEL,scope='host_protocol_only',hardware_tools_executed=False,audio_played=False,
        config=config,answer_instructions=ANSWER,answer_tools=[RGB_TOOL],turns=[],
        script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        fixtures=[dict(id=r['id'],text=r['text'],path=r['path'],samples=len(pcm)//2,
                       source_sha256=hashlib.sha256(Path(r['path']).read_bytes()).hexdigest(),
                       pcm_sha256=hashlib.sha256(pcm).hexdigest()) for r,pcm in fixtures])
    def save():
        (args.out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    save()
    if not args.execute:
        print(json.dumps(dict(model=MODEL,sessions=1,turns=3,execute=False)));return
    import websocket
    from qianwen_credentials import qianwen_key
    key=qianwen_key();ws=worker=None;stopped=threading.Event();incoming=queue.Queue(maxsize=256)
    started=time.monotonic();nonce=0;current=None;phase=None;audios={}
    seen_inputs=set();seen_responses=set();seen_calls=set()
    stamp=lambda:round(time.monotonic()-started,6)
    def record(direction,at,event):
        with (args.out/'wire.jsonl').open('a',encoding='utf8') as log:
            log.write(json.dumps(dict(direction=direction,observed_s=at,
                turn=current['id'] if current else None,phase=phase,event=event),ensure_ascii=False).replace(key,'[REDACTED]')+'\n')
    def send(kind,**fields):
        nonlocal nonce
        nonce+=1;value=dict(type=kind,event_id=f'final_intent_{nonce}',**fields)
        ws.send(json.dumps(value,ensure_ascii=False,separators=(',',':')));record('send',stamp(),value)
    def receiver():
        try:
            while not stopped.is_set():
                raw=ws.recv()
                if not raw:
                    incoming.put((stamp(),None,'eof'),timeout=1);return
                incoming.put((stamp(),raw,None),timeout=2)
        except Exception as error:
            if not stopped.is_set():
                try:incoming.put((stamp(),None,type(error).__name__),timeout=1)
                except queue.Full:pass
    def receive(timeout=.01):
        try:at,raw,cause=incoming.get(timeout=timeout)
        except queue.Empty:return None,None
        if raw is None:raise RuntimeError('Transport stopped: '+cause)
        if len(raw)>2*1024*1024:raise ValueError('Event too large')
        e=json.loads(raw);record('receive',at,e);kind=e.get('type')
        if kind=='error':raise RuntimeError('Provider error retained in wire')
        if current is None:return at,e
        if kind=='input_audio_buffer.committed':
            ident=e.get('item_id')
            if not current.get('commit_s') or not ident or ident in seen_inputs or current.get('input_id'):
                raise ValueError('Invalid committed identity')
            current['input_id']=ident;seen_inputs.add(ident)
        elif kind=='conversation.item.input_audio_transcription.completed':
            if not current.get('input_id') or e.get('item_id')!=current['input_id'] or current.get('final_asr'):
                raise ValueError('Stale/duplicate final ASR')
            current.update(final_asr=e.get('transcript'),final_asr_s=at)
        elif kind=='response.created':
            ident=e.get('response',{}).get('id')
            if phase not in ('prepare','answer') or not ident or ident in seen_responses:
                raise ValueError('Stale response')
            seen_responses.add(ident);current[phase]['ids'].append(ident)
            if len(current[phase]['ids'])>3:raise ValueError('Response transition bound')
        elif kind=='response.done':
            response=e.get('response',{});ident=response.get('id')
            if phase not in ('prepare','answer') or ident not in current[phase]['ids'] or response.get('status')!='completed':
                raise ValueError('Unmatched/incomplete response')
            current[phase]['done_ids'].append(ident);current[phase]['done_s']=at
        elif kind.startswith('response.') and e.get('response_id'):
            if phase not in ('prepare','answer') or e['response_id'] not in current[phase]['ids']:
                raise ValueError('Response event outside its phase')
            stage=current[phase]
            if kind=='response.function_call_arguments.done':
                ident=e.get('call_id')
                if not ident or ident in seen_calls:raise ValueError('Reused call identity')
                seen_calls.add(ident);call=dict(e,observed_s=at)
                call['parsed']=json.loads(e['arguments']);stage['calls'].append(call)
                if len(stage['calls'])>1:raise ValueError('Only one call per phase allowed')
            elif kind in ('response.text.delta','response.audio_transcript.delta'):
                stage['text']+=e.get('delta','')
                if len(stage['text'].encode())>2048:raise ValueError('Text limit')
            elif kind=='response.audio.delta':
                data=base64.b64decode(e['delta'],validate=True)
                if len(data)%2:raise ValueError('Odd PCM')
                audio=audios.setdefault((current['id'],phase),bytearray());audio.extend(data)
                stage.setdefault('first_pcm_s',at)
                if len(audio)>16000*2*10:raise ValueError('PCM limit')
        return at,e
    def wait_for(kind,seconds=5):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            at,e=receive(.02)
            if e and e.get('type')==kind:return at,e
        raise TimeoutError(kind)
    def update(instructions,tools):
        send('session.update',session=config|dict(instructions=instructions,tools=tools))
        _,e=wait_for('session.updated')
        session=e['session'];fmt=session.get('audio',{}).get('output',{}).get('format',{})
        if session.get('instructions')!=instructions or fmt.get('type')!='pcm' or fmt.get('sample_rate')!=16000:
            raise ValueError('Session configuration echo mismatch')
    def stage():return dict(ids=[],done_ids=[],calls=[],text='')
    def finished(stage):
        if stage['calls']:return stage['calls'][-1]['response_id'] in stage['done_ids']
        return bool(stage['done_ids']) and stage['done_ids'][-1]==stage['ids'][-1]
    try:
        report['attempts']=1;save()
        ws=websocket.create_connection('wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model='+MODEL,
            header=['Authorization: Bearer '+key],timeout=20,redirect_limit=0,enable_multithread=True)
        worker=threading.Thread(target=receiver,daemon=True);worker.start()
        _,e=wait_for('session.created');report['session_id']=e['session']['id']
        for index,(r,pcm) in enumerate(fixtures):
            current=dict(id=r['id'],prepare=stage(),answer=stage(),final_asr=None,complete=False)
            report['turns'].append(current);phase=None
            update(PREP,[WAIT_TOOL]);phase='prepare'
            start=stamp();offset=0;request=False;committed=False
            end=time.monotonic()+30
            while time.monotonic()<end:
                receive(.003)
                if offset<len(pcm) and stamp()>=start+offset/32000:
                    chunk=pcm[offset:offset+1024];send('input_audio_buffer.append',audio=base64.b64encode(chunk).decode())
                    offset+=len(chunk)
                if not request and offset>=48000:
                    current['prepare_requested_s']=stamp();send('response.create');request=True
                if offset==len(pcm) and not committed:
                    current['commit_s']=stamp();send('input_audio_buffer.commit');committed=True
                if current['final_asr'] and finished(current['prepare']):
                    if not current['prepare']['calls']:raise ValueError('Preparation tool not called')
                    break
            current['sent_samples']=offset//2
            current['complete_input']=bool(current['final_asr'] and complete_input(r['text'],current['final_asr']))
            calls=current['prepare']['calls']
            if not current['complete_input'] or not finished(current['prepare']) or len(calls)!=1:
                raise ValueError('Complete input and one preparation call required')
            call=calls[0]
            if call['name']!='await_final_input' or call['parsed']!={}:raise ValueError('Invalid preparation call')
            # Read-only real result, no invented model input or device success.
            result=dict(input_item_id=current['input_id'],transcript=current['final_asr'],
                        final=True,device_actions_executed=False)
            phase=None;update(ANSWER,[RGB_TOOL]);phase='answer'
            current['result_sent_s']=stamp();current['preparation_result']=result
            send('conversation.item.create',item=dict(type='function_call_output',call_id=call['call_id'],
                                                      output=json.dumps(result,ensure_ascii=False)))
            send('response.create')
            end=time.monotonic()+20
            while time.monotonic()<end and not finished(current['answer']):receive(.01)
            final=current['answer'];calls=final['calls']
            if r['id']=='greeting':
                valid=not calls and '小言' in final['text'] and bool(audios.get((r['id'],'answer')))
            else:
                chosen=calls[0]['parsed'] if len(calls)==1 else {}
                colour='g' if r['id']=='correction' else 'b'
                valid=(len(calls)==1 and calls[0]['name']=='device_light_set_rgb' and set(chosen)==set('rgb')
                    and all(type(chosen[c]) is int and 0<=chosen[c]<=255 for c in 'rgb')
                    and chosen[colour]>0 and all(chosen[c]==0 for c in 'rgb' if c!=colour))
            current['complete']=finished(final) and valid
            save()
            if not current['complete']:raise ValueError('Final response/proposal did not match full intent')
            print(json.dumps(dict(turn=r['id'],complete=True,asr=current['final_asr'],text=final['text'],
                proposals=[dict(name=c['name'],arguments=c['parsed']) for c in calls]),ensure_ascii=False),flush=True)
            for proposed in calls:
                # Close outstanding calls truthfully so the next input does not
                # inherit an unresolved proposal. Do not generate a fake reply.
                send('conversation.item.create',item=dict(type='function_call_output',call_id=proposed['call_id'],
                    output=json.dumps(dict(executed=False,reason='Host protocol probe has no physical device'))))
            phase=None;send('input_audio_buffer.clear');wait_for('input_audio_buffer.cleared',2)
            # No proposal is executed or given a fake success result.
        report['complete']=True
    except Exception as error:
        report.update(error_type=type(error).__name__,reason=str(error).replace(key,'[REDACTED]')[:180])
    finally:
        stopped.set()
        if ws is not None:ws.close(timeout=2)
        if worker is not None:worker.join(timeout=3)
        for (ident,part),data in audios.items():
            path=args.out/f'{ident}-{part}.wav'
            with wave.open(str(path),'wb') as wav:
                wav.setparams((1,2,16000,0,'NONE','not compressed'));wav.writeframes(data)
        report['finished_s']=stamp();save()
    print(json.dumps(dict(complete=report['complete'],turns=len(report['turns']),error=report.get('reason')),ensure_ascii=False))
    if not report['complete']:raise SystemExit(1)


if __name__=='__main__':main()
