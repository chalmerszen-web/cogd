"""Bounded cold-session tool-adoption probe, synthetic audio and mock history.

Uses the current firmware's exact prompt/schema. No microphone, physical tool,
automatic retry or reconnect. Host timing is never device/acoustic timing.
"""
import argparse
import ast
import base64
import hashlib
import json
from pathlib import Path
import re
import time

import websocket
from omni_realtime_probe import MODEL, URL, prepare_source
from qianwen_credentials import qianwen_key

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--trials', type=int, choices=(1, 2, 3), default=1)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    source = ROOT/'platform/espidf/voice_fast.c'
    text = source.read_text(encoding='utf8')
    def constant(name):
        body = text.split('static const char '+name+'[]=', 1)[1].split(';\n', 1)[0]
        return ''.join(ast.literal_eval(t) for t in re.findall(r'"(?:\\.|[^"\\])*"', body))
    config = dict(modalities=['text', 'audio'], voice='Tina', input_audio_format='pcm',
                  output_audio_format='pcm', enable_search=False, turn_detection=None,
                  instructions=constant('instructions'), tools=json.loads(constant('tools')))
    manifest = json.loads((ROOT/'artifacts/voice-cloud/prompts-01/manifest.json').read_text(encoding='utf8'))
    assert manifest['synthetic'] and manifest['complete']
    row = next(r for r in manifest['prompts'] if r['id'] == 'recall_name')
    pcm, facts = prepare_source(row, args.out)
    report = dict(scope='host_mock_only', model=MODEL, retries=0, source=facts,
                  source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), config=config, trials=[])
    key = qianwen_key()
    def save():
        (args.out/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    save()
    for index in range(args.trials):
        trial = dict(trial=index+1, complete=False, calls=[], texts='', audio_bytes=0)
        report['trials'].append(trial)
        ws = None; start = time.monotonic(); nonce = 0
        def send(value):
            nonlocal nonce
            nonce += 1
            value = dict(event_id='c'+str(nonce), **value)
            wire = json.dumps(value, ensure_ascii=False, separators=(',', ':'))
            assert len(wire.encode()) < 2048
            ws.send(wire)
        def until(kind, seconds=12):
            end = time.monotonic()+seconds
            while time.monotonic() < end:
                ws.settimeout(max(.01, end-time.monotonic()))
                raw = ws.recv()
                if not raw: raise RuntimeError('closed')
                event = json.loads(raw.replace(key, '[REDACTED]'))
                stamp = time.monotonic()-start
                typ = event.get('type')
                if typ == 'response.audio.delta':
                    n = len(base64.b64decode(event.pop('delta'), validate=True))
                    trial['audio_bytes'] += n;event['audio_bytes'] = n
                if typ == 'response.audio_transcript.delta': trial['texts'] += event['delta']
                if typ == 'response.function_call_arguments.done':
                    trial['calls'].append(dict(at_s=stamp, event=event))
                with (args.out/f'events-{index+1:02}.jsonl').open('a', encoding='utf8') as f:
                    f.write(json.dumps(dict(at_s=stamp, event=event), ensure_ascii=False)+'\n')
                if typ == 'error': raise RuntimeError('provider_error')
                if typ == kind: return event
            raise TimeoutError('probe_deadline')
        try:
            ws = websocket.create_connection(URL, header=['Authorization: Bearer '+key],
                                             timeout=12, redirect_limit=0)
            until('session.created')
            send(dict(type='session.update', session=config));until('session.updated')
            began = time.monotonic()
            for offset in range(0, len(pcm), 1024):
                chunk = pcm[offset:offset+1024]
                time.sleep(max(0, began+(offset+len(chunk))/32000-time.monotonic()))
                send(dict(type='input_audio_buffer.append', audio=base64.b64encode(chunk).decode()))
            trial['commit_s'] = time.monotonic()-start
            send(dict(type='input_audio_buffer.commit'));send(dict(type='response.create'))
            response = until('response.done')['response']
            trial['initial_done_s'] = time.monotonic()-start
            calls = [item for item in response.get('output', []) if item.get('type') == 'function_call']
            trial['completed_calls'] = calls
            if response.get('status') != 'completed': raise RuntimeError('incomplete_response')
            if len(calls) == 1 and calls[0]['name'] == 'agent_context_search':
                call = calls[0];query = json.loads(call['arguments'])
                assert set(query) == {'query'} and isinstance(query['query'], str) and 0 < len(query['query'].encode()) <= 128
                excerpt = '本次电脑模拟记录：这盏灯的名字是小星星。'
                hits = [dict(event_id='host:1', record_seq=1, excerpt=excerpt)] if query['query'] in excerpt else []
                output = dict(host_mock=True, hits=hits, more=False, next_before=1 if hits else 0)
                trial['mock_result'] = output
                send(dict(type='conversation.item.create', item=dict(type='function_call_output',
                          call_id=call['call_id'], output=json.dumps(output, ensure_ascii=False))))
                send(dict(type='response.create'));trial['continuation'] = until('response.done')['response']
            trial['complete'] = True
        except Exception as exc:
            trial['error_type'] = type(exc).__name__
        finally:
            if ws: ws.close()
            trial['elapsed_s'] = time.monotonic()-start;save()
        print(json.dumps(trial, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()
