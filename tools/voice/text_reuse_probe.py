"""One verified WSS connection, three text candidates; no device/audio capture.

No retry, playback or tool execution. Raw receipts and bounded PCM stay local.
This tests the provider protocol, not C3 memory, audio quality or response speed.
"""
import argparse
import base64
import json
from pathlib import Path
import time
import wave

from text_candidate_probe import CASES, CONFIG as ORIGINAL_CONFIG, MODEL, URL
from text_candidate_probe import content_check, item_matches, sha, user_item

CONFIG = dict(ORIGINAL_CONFIG, instructions=(
    'ESP-HI助手小言。默认普通话，仅要求时用粤语，一句≤14字。'
    '介绍直接答；动作未执行，只表准备，禁报完成、参数或流程。'
    '灯光：嗯，我来调整一下灯光。记忆：嗯，我来记一下。'))


def run(out, execute=False, pipeline=False):
    requests = [user_item(case) for case in CASES]
    out.mkdir(parents=True, exist_ok=False)
    report = dict(model=MODEL, config=CONFIG, execute=execute, pipelined_sender=pipeline, session_limit=1,
                  turns=3, per_turn_seconds=25, group_seconds=80, retries=0,
                  played=False, tools_executed=False, microphone=False, device=False,
                  complete=False, sessions_opened=0, rounds=[],
                  script_sha256=sha(Path(__file__).read_bytes()),
                  dependency_sha256=sha(Path(__file__).with_name('text_candidate_probe.py').read_bytes()),
                  limitation='Host protocol only. No C3 latency/resource or microphone acceptance.')

    def save():
        (out / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf8')

    (out / 'executed-script.py').write_bytes(Path(__file__).read_bytes())
    save()
    if not execute:
        return report
    start = time.monotonic()
    group_end = start + 80
    ws = None
    ids, response_ids = set(), set()
    try:
        import websocket
        from qianwen_credentials import qianwen_key
        key = qianwen_key()
        ws = websocket.create_connection(URL, header=['Authorization: Bearer '+key],
                                         timeout=5, redirect_limit=0)
        report.update(sessions_opened=1, connected_s=time.monotonic()-start)
        for index, (case, request) in enumerate(zip(CASES, requests), 1):
            directory = out / ('turn-%02d' % index)
            directory.mkdir()
            (directory / 'request.json').write_text(json.dumps(request, ensure_ascii=False), encoding='utf8')
            entry = dict(round=index, case=case, complete=False, events=0)
            report['rounds'].append(entry)
            deadline = min(group_end, time.monotonic()+25)
            pcm, text, response_id = bytearray(), '', None
            terminal_text = None
            received_bytes = 0

            def send(event):
                left = deadline-time.monotonic()
                if left <= 0:
                    raise TimeoutError('Turn deadline')
                event = dict(event, event_id=f'ux115_{index}_{time.monotonic_ns()}')
                wire = json.dumps(event, ensure_ascii=False)
                if len(wire.encode('utf8')) >= 2048:
                    raise ValueError('TX budget')
                ws.settimeout(min(2, left))
                at = time.monotonic()-start
                ws.send(wire)
                with (directory / 'sent.jsonl').open('a', encoding='utf8') as f:
                    f.write(json.dumps(dict(at_s=at,event=event), ensure_ascii=False)+'\n')
                return at

            def receive():
                nonlocal received_bytes
                left = deadline-time.monotonic()
                if left <= 0:
                    raise TimeoutError('Turn deadline')
                ws.settimeout(min(5, left))
                raw = ws.recv()
                if not isinstance(raw, str) or not raw:
                    raise ValueError('Unexpected binary/close')
                received_bytes += len(raw.encode('utf8'))
                if received_bytes > 1048576 or len(raw.encode('utf8')) > 262000:
                    raise ValueError('RX budget')
                at = time.monotonic()-start
                with (directory / 'received.jsonl').open('a', encoding='utf8') as f:
                    f.write(raw+'\n')
                event = json.loads(raw)
                with (directory / 'received-times.jsonl').open('a', encoding='utf8') as f:
                    f.write(json.dumps(dict(at_s=at,type=event.get('type'),sha256=sha(raw.encode('utf8'))))+'\n')
                entry['events'] += 1
                if event.get('type') == 'error':
                    entry['provider_error'] = event
                    raise RuntimeError('Provider error; raw receipt retained')
                return at, event

            try:
                if index == 1:
                    _, event = receive()
                    if event.get('type') != 'session.created' or event.get('session', {}).get('model') != MODEL:
                        raise ValueError('Unexpected session')
                    report['session_id'] = event['session']['id']
                    send(dict(type='session.update', session=CONFIG))
                    _, event = receive()
                    session = event.get('session', {})
                    keys = ('modalities','voice','turn_detection','enable_search','max_tokens','instructions')
                    if (event.get('type') != 'session.updated' or session.get('model') != MODEL or
                            any(session.get(k) != CONFIG[k] for k in keys) or session.get('tools') not in (None,[]) or
                            session.get('audio',{}).get('output',{}).get('format') != CONFIG['audio']['output']['format']):
                        raise ValueError('Configuration mismatch')
                entry['text_sent_s'] = send(request)
                if pipeline:
                    entry['response_request_s'] = send(dict(type='response.create'))
                ack_at, event = receive()
                if not item_matches(event, case) or event['item']['id'] in ids:
                    raise ValueError('Text identity/acknowledgement mismatch')
                ids.add(event['item']['id']);entry['input_id'] = event['item']['id']
                entry['text_acked_s'] = ack_at
                if not pipeline:
                    entry['response_request_s'] = send(dict(type='response.create'))
                while True:
                    at, event = receive()
                    kind = event.get('type','')
                    if kind == 'response.created':
                        ident = event.get('response',{}).get('id')
                        if response_id is not None or not isinstance(ident,str) or not 0 < len(ident) <= 64 or ident in response_ids:
                            raise ValueError('Reused/invalid response ID')
                        response_id = ident;response_ids.add(ident)
                    elif kind in ('response.audio.delta','response.audio_transcript.delta','response.audio_transcript.done','response.audio.done'):
                        if not response_id or event.get('response_id') != response_id:
                            raise ValueError('Mismatched response ID')
                        if kind == 'response.audio.delta':
                            block = base64.b64decode(event['delta'],validate=True)
                            if block:
                                entry.setdefault('first_pcm_s',at)
                            pcm.extend(block)
                            if len(pcm) > 192000:
                                raise ValueError('PCM budget')
                        elif kind == 'response.audio_transcript.delta':
                            text += event['delta']
                            if len(text.encode('utf8')) > 512:
                                raise ValueError('Text budget')
                        elif kind == 'response.audio_transcript.done':
                            if terminal_text is not None or event.get('transcript') != text:
                                raise ValueError('Transcript mismatch')
                            terminal_text = text
                    elif kind == 'response.done':
                        response = event.get('response',{})
                        if not response_id or response.get('id') != response_id or response.get('status') != 'completed':
                            raise ValueError('Incomplete response')
                        if not pcm or len(pcm)%2 or not terminal_text or any(i.get('type') != 'message' for i in response.get('output',[])):
                            raise ValueError('Incomplete text/PCM or unexpected tool')
                        entry.update(complete=True,response_done_s=at,response_id=response_id)
                        break
                    elif kind.startswith(('response.function_call','input_audio_buffer.','session.')):
                        raise ValueError('Unexpected input/tool/session event')
                entry.update(text=text,content_pass=content_check(case,text),
                             first_pcm_after_request_ms=1000*(entry['first_pcm_s']-entry['response_request_s']))
            finally:
                (directory / 'output.pcm').write_bytes(pcm)
                if len(pcm)%2 == 0:
                    with wave.open(str(directory / 'output.wav'),'wb') as f:
                        f.setparams((1,2,16000,0,'NONE','not compressed'));f.writeframes(pcm)
                entry.update(pcm_bytes=len(pcm),pcm_sha256=sha(pcm),received_bytes=received_bytes)
                save()
            print(json.dumps(entry,ensure_ascii=False),flush=True)
        report['complete'] = len(report['rounds']) == 3 and all(r['complete'] for r in report['rounds'])
    except Exception as error:
        report['error'] = type(error).__name__+': '+str(error)
        raise
    finally:
        if ws is not None:
            ws.close(timeout=1)
        report['closed'] = True
        report['elapsed_s'] = time.monotonic()-start
        save()
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--execute',action='store_true')
    parser.add_argument('--pipeline',action='store_true',help='Send response.create before waiting for the exact text receipt')
    args = parser.parse_args()
    print(json.dumps(dict(complete=run(args.out,args.execute,args.pipeline)['complete'])))
