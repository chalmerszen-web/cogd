"""Three bounded text-to-candidate sessions; no microphone, playback or tools.

This verifies a protocol primitive, not ASR overlap or device response latency.
Each execution directory is single-use. No retry or model fallback.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import time
import wave

MODEL = 'qwen3.5-omni-flash-realtime'
URL = 'wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model=' + MODEL
CASES = [
    dict(id='ux66_zh', text='请用一句话介绍你自己。', kind='self', language='zh'),
    dict(id='ux66_light', text='请准备调整灯光，具体颜色稍后确定。', kind='light', language='zh'),
    dict(id='ux66_yue', text='你係邊個？用一句廣東話介紹自己。', kind='self', language='yue'),
]
INSTRUCTIONS = ('你是小言，ESP-HI硬件助手。用一句简短口语回复，最多14个汉字。'
                '介绍自己时直接回答；用户要求广东话时用广东话。'
                '设备动作尚未执行，只能用未来式表示准备，不说完成，不猜颜色或数值。'
                '灯光准备自然说“嗯，我来调整一下灯光。”。不谈内部流程。')
CONFIG = dict(modalities=['text', 'audio'], voice='Tina', input_audio_format='pcm',
              audio=dict(output=dict(format=dict(type='pcm', sample_rate=16000))),
              turn_detection=None, tools=[], enable_search=False, max_tokens=96,
              instructions=INSTRUCTIONS)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def user_item(case):
    ident, text = case['id'], case['text']
    if not isinstance(ident, str) or not 0 < len(ident.encode('utf8')) <= 64:
        raise ValueError('Invalid item ID')
    if not isinstance(text, str) or not 0 < len(text.encode('utf8')) <= 512:
        raise ValueError('Invalid user text')
    return dict(type='conversation.item.create', item=dict(type='message',
                role='user', content=[dict(type='input_text', text=text)]))


def item_matches(event, case):
    expected = user_item(case)['item']
    item = event.get('item')
    return (event.get('type') == 'conversation.item.created' and isinstance(item, dict)
            and isinstance(item.get('id'), str) and 0 < len(item['id'].encode('utf8')) <= 64
            and item.get('status') == 'completed'
            and all(item.get(k) == v for k, v in expected.items()))


def config_matches(session):
    return (session.get('model') == MODEL and
            all(session.get(k) == CONFIG[k] for k in ('modalities', 'voice', 'turn_detection',
                'enable_search', 'max_tokens', 'instructions')) and
            # On a fresh session the actual empty tool list is omitted from
            # the echo. Nonempty tools remain rejected; no tool is executed.
            session.get('tools') in (None, []) and
            session.get('audio', {}).get('output', {}).get('format') == CONFIG['audio']['output']['format'])


def content_check(case, text):
    if case['kind'] == 'self':
        return '小言' in text and not any(x in text for x in ('草稿', '转写', '轉寫'))
    return text.strip(' 。！!\n') in ('嗯，我来调整一下灯光', '嗯，我来调整灯光')


def run(out, execute=False):
    # Do all pure validation before importing credential/network code.
    requests = [user_item(c) for c in CASES]
    out.mkdir(parents=True, exist_ok=False)
    report = dict(model=MODEL, execute=execute, config=CONFIG, cases=CASES,
                  script_sha256=sha(Path(__file__).read_bytes()),
                  session_limit=3, per_session_seconds=25, group_seconds=90,
                  max_pcm_bytes=192000, retries=0, played=False, tools_executed=False,
                  microphone=False, device=False, complete=False, sessions=[],
                  limitation='Text protocol only; no ASR overlap, acoustic or C3 resource claim.')
    (out / 'executed-script.py').write_bytes(Path(__file__).read_bytes())
    def save():
        (out / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    save()
    if not execute:
        return report
    import websocket
    from qianwen_credentials import qianwen_key
    key = qianwen_key()
    group_deadline = time.monotonic() + 90
    try:
        for index, (case, request) in enumerate(zip(CASES, requests), 1):
            directory = out / case['id']
            directory.mkdir()
            (directory / 'request.json').write_text(json.dumps(request, ensure_ascii=False), encoding='utf8')
            entry = dict(id=case['id'], attempts=1, complete=False, events=0)
            report['sessions'].append(entry)
            save()
            start = time.monotonic()
            deadline = min(group_deadline, start+25)
            ws = None
            pcm = bytearray()
            response_id = None
            text = ''
            terminal_text = None
            received_bytes = 0
            try:
                ws = websocket.create_connection(URL, header=['Authorization: Bearer '+key],
                    timeout=min(5, max(.01, deadline-time.monotonic())), redirect_limit=0)
                entry['connected_s'] = time.monotonic()-start
                def log(name, value):
                    with (directory / name).open('a', encoding='utf8') as stream:
                        stream.write(json.dumps(value, ensure_ascii=False)+'\n')
                def send(event):
                    remaining = deadline-time.monotonic()
                    if remaining <= 0:
                        raise TimeoutError('Session deadline')
                    event = dict(event, event_id=f'ux66_{index}_{time.monotonic_ns()}')
                    wire = json.dumps(event, ensure_ascii=False)
                    if len(wire.encode('utf8')) >= 2048:
                        raise ValueError('TX budget')
                    ws.settimeout(min(2, remaining))
                    at = time.monotonic()-start
                    ws.send(wire)
                    log('sent.jsonl', dict(at_s=at, event=event, sha256=sha(wire.encode('utf8'))))
                    return at
                def receive():
                    nonlocal received_bytes
                    remaining = deadline-time.monotonic()
                    if remaining <= 0:
                        raise TimeoutError('Session deadline')
                    ws.settimeout(min(5, remaining))
                    raw = ws.recv()
                    if not isinstance(raw, str) or not raw:
                        raise ValueError('Unexpected binary/close')
                    received_bytes += len(raw.encode('utf8'))
                    if received_bytes > 1048576 or len(raw.encode('utf8')) > 262000:
                        raise ValueError('RX budget')
                    at = time.monotonic()-start
                    with (directory / 'received.jsonl').open('a', encoding='utf8') as stream:
                        stream.write(raw+'\n')
                    event = json.loads(raw)
                    log('received-times.jsonl', dict(at_s=at, type=event.get('type'),
                                                    sha256=sha(raw.encode('utf8'))))
                    entry['events'] += 1
                    if event.get('type') in ('error', 'conversation.item.input_audio_transcription.failed'):
                        entry['provider_error'] = event
                        raise RuntimeError('Provider error; raw receipt retained')
                    return at, event
                _, event = receive()
                if event.get('type') != 'session.created' or event.get('session', {}).get('model') != MODEL:
                    raise ValueError('Unexpected session')
                send(dict(type='session.update', session=CONFIG))
                _, event = receive()
                if event.get('type') != 'session.updated' or not config_matches(event.get('session', {})):
                    raise ValueError('Configuration acknowledgement mismatch')
                entry['ready_s'] = time.monotonic()-start
                entry['text_sent_s'] = send(request)
                _, event = receive()
                if not item_matches(event, case):
                    raise ValueError('Text item acknowledgement mismatch')
                entry['server_input_id'] = event['item']['id']
                entry['item_ack_s'] = time.monotonic()-start
                entry['response_request_s'] = send(dict(type='response.create'))
                while True:
                    at, event = receive()
                    kind = event.get('type', '')
                    if kind == 'response.created':
                        if response_id is not None:
                            raise ValueError('Second response')
                        response_id = event.get('response', {}).get('id')
                        if not isinstance(response_id, str) or not 0 < len(response_id) <= 64:
                            raise ValueError('Invalid response ID')
                    elif kind in ('response.audio.delta', 'response.audio_transcript.delta',
                                  'response.audio_transcript.done', 'response.audio.done'):
                        if not response_id or event.get('response_id') != response_id:
                            raise ValueError('Mismatched response ID')
                        if kind == 'response.audio.delta':
                            block = base64.b64decode(event['delta'], validate=True)
                            if block:
                                entry.setdefault('first_pcm_s', at)
                            pcm.extend(block)
                            if len(pcm) > report['max_pcm_bytes']:
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
                        response = event.get('response', {})
                        if not response_id or response.get('id') != response_id or response.get('status') != 'completed':
                            raise ValueError('Incomplete response')
                        if not pcm or len(pcm) % 2 or not terminal_text:
                            raise ValueError('Missing complete audio/text')
                        if any(item.get('type') != 'message' for item in response.get('output', [])):
                            raise ValueError('Unexpected tool output')
                        entry.update(complete=True, response_done_s=at)
                        break
                    elif kind.startswith('response.function_call') or kind.startswith('input_audio_buffer.'):
                        raise ValueError('Unexpected input/tool event')
                entry.update(text=text, content_pass=content_check(case, text),
                    first_pcm_after_request_ms=1000*(entry['first_pcm_s']-entry['response_request_s']))
            finally:
                if ws is not None:
                    ws.close(timeout=1)
                (directory / 'output.pcm').write_bytes(pcm)
                if len(pcm) % 2 == 0:
                    with wave.open(str(directory / 'output.wav'), 'wb') as output:
                        output.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
                        output.writeframes(pcm)
                entry.update(pcm_bytes=len(pcm), pcm_sha256=sha(pcm),
                    pcm_seconds=len(pcm)/32000, ima_cache_estimate_bytes=(len(pcm)//2+1)//2,
                    elapsed_s=time.monotonic()-start)
                save()
            print(json.dumps(entry, ensure_ascii=False), flush=True)
        report['complete'] = len(report['sessions']) == 3 and all(s['complete'] for s in report['sessions'])
    except Exception as error:
        report['error'] = type(error).__name__+': '+str(error)
        raise
    finally:
        save()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    result = run(args.out, args.execute)
    print(json.dumps(dict(complete=result['complete'], executed=args.execute)))


if __name__ == '__main__':
    main()
