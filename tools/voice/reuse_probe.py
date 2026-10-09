"""Three manual turns on one bounded WS session, with an acknowledged input clear.

Existing synthetic fixtures only; no microphone, speaker, device actions or retries.
The clear barrier resets uncommitted audio, NOT server conversation history.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import queue
import re
import subprocess
import threading
import time
import wave

from input_integrity import complete_input

MODEL = 'qwen3.5-omni-flash-realtime'


def device_prediction():
    source = Path('platform/espidf/voice_fast.c')
    text = source.read_text(encoding='utf8')
    match = re.search(r'static const char prediction_instructions\[\]\s*=([^;]+);', text)
    if not match:
        raise ValueError('Missing device prediction instructions')
    literals = re.findall(r'"(?:[^"\\]|\\.)*"', match[1])
    if not literals:
        raise ValueError('Missing device prediction string literals')
    return ''.join(json.loads(value) for value in literals), hashlib.sha256(source.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--execute', action='store_true')
    p.add_argument('--manual-draft', action='store_true',
                   help='Create once before whole-input commit; keep a bounded silent tail')
    p.add_argument('--output-rate', type=int, choices=(16000, 24000), default=24000)
    p.add_argument('--fixture-ids', nargs=3, default=('greeting', 'blue', 'green'))
    p.add_argument('--chunk-samples', type=int, choices=(464, 512), default=512)
    p.add_argument('--lead-ms', type=int, choices=(0, 400), default=0)
    p.add_argument('--tail-ms', type=int, choices=(200, 700), default=200)
    p.add_argument('--draft-after-samples', type=int, default=0,
                   help='Fixed input-clock trigger for diagnosis; zero uses original file end')
    p.add_argument('--device-prediction', action='store_true')
    p.add_argument('--observe-missing-final', action='store_true',
                   help='Observe for 8s after commit; advance only after ACK and completed response')
    p.add_argument('--confirm-missing-final', action='store_true',
                   help='One additional generation after complete commit and draft completion; host diagnosis only')
    args = p.parse_args()
    if args.draft_after_samples < 0 or args.draft_after_samples > 16000*8:
        raise ValueError('Invalid draft sample trigger')
    if (args.device_prediction or args.observe_missing_final or args.lead_ms or
            args.tail_ms != 200 or args.draft_after_samples) and not args.manual_draft:
        raise ValueError('Diagnostic options require manual draft mode')
    if args.device_prediction and args.output_rate != 16000:
        raise ValueError('Device prediction requires the actual16k output rate')
    if args.confirm_missing_final and not (args.manual_draft and args.observe_missing_final):
        raise ValueError('Confirmation observation requires manual draft and missing-final observation')
    args.out.mkdir(parents=True, exist_ok=False)
    script_bytes = Path(__file__).read_bytes()
    (args.out/'executed-script.py').write_bytes(script_bytes)
    manifest = Path('artifacts/voice-cloud/prompts-01/manifest.json')
    prompts = {r['id']: r for r in json.loads(manifest.read_text(encoding='utf8'))['prompts']}
    cases = []
    for ident in args.fixture_ids:
        row = prompts[ident]
        source = Path(row['path'])
        pcm = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(source), '-ar', '16000',
            '-ac', '1', '-f', 's16le', '-'], check=True, capture_output=True, timeout=15).stdout
        if not pcm or len(pcm) % 2 or len(pcm) > 32000 * 6:
            raise ValueError('Invalid bounded synthetic fixture')
        cases.append((row, pcm))
    prepared = []
    for row, source_pcm in cases:
        pcm = bytes(args.lead_ms*32)+source_pcm
        trigger_samples = args.draft_after_samples or len(pcm)//2
        if args.manual_draft:
            pcm += bytes(args.tail_ms*32)
            if trigger_samples >= len(pcm)//2:
                raise ValueError('Draft trigger must precede full input commit')
        prepared.append((row, pcm, trigger_samples))
    config = dict(modalities=['text', 'audio'], voice='Tina', input_audio_format='pcm',
        output_audio_format='pcm', turn_detection=None, enable_search=False, tools=[],
        instructions='你是小言。最多16字，简短回答。这是协议测试，没有实际连接硬件；灯色指令只说准备设成什么颜色，不能说已完成。')
    if args.manual_draft:
        from manual_draft_probe import PRODUCT
        config.update(instructions=PRODUCT, max_tokens=128)
    prediction_source_sha = None
    if args.device_prediction:
        config['instructions'], prediction_source_sha = device_prediction()
        config.pop('max_tokens', None)  # Match the device, including its default output cap.
    if args.output_rate != 24000:
        config.pop('output_audio_format')
        config['audio'] = dict(output=dict(format=dict(type='pcm', sample_rate=args.output_rate)))
    report = dict(accepted=False, execute=args.execute, model=MODEL, attempts=0, complete=False,
        scope='synthetic_host_protocol_only', config=config, turns=[], retries=0,
        manual_draft=args.manual_draft, played=False, tools_executed=False,
        output_rate=args.output_rate,
        prediction_source_sha256=prediction_source_sha,
        chunk_samples=args.chunk_samples, lead_ms=args.lead_ms, tail_ms=args.tail_ms,
        draft_after_samples=args.draft_after_samples,
        observe_missing_final=args.observe_missing_final,
        confirm_missing_final=args.confirm_missing_final,
        max_model_responses=6 if args.confirm_missing_final else 3,
        script_sha256=hashlib.sha256(script_bytes).hexdigest(),
        manifest_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest(),
        inputs=[dict(id=r['id'], samples=len(pcm)//2,
                     pcm_sha256=hashlib.sha256(pcm).hexdigest(), trigger_samples=trigger)
                for r, pcm, trigger in prepared],
        fixtures=[dict(id=r['id'], text=r['text'], pcm_sha256=hashlib.sha256(pcm).hexdigest(),
                       samples=len(pcm)//2) for r, pcm in cases])
    def save():
        (args.out/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    save()
    if not args.execute:
        print(json.dumps(dict(model=MODEL, sessions=1, turns=3, execute=False)))
        return
    import websocket
    from qianwen_credentials import qianwen_key
    key = qianwen_key()
    ws = worker = None
    stopped = threading.Event()
    incoming = queue.Queue(maxsize=256)
    started = time.monotonic()
    session_deadline = started+120
    nonce = 0
    audios = {}
    def stamp():
        return round(time.monotonic()-started, 6)
    def receiver():
        failure = None
        try:
            while not stopped.is_set():
                raw = ws.recv()
                if not raw:
                    failure = dict(kind='empty_receive', observed_s=stamp())
                    break
                incoming.put((stamp(), raw), timeout=2)
        except Exception as exc:
            failure = dict(kind=type(exc).__name__, message=str(exc).replace(key, '[REDACTED]')[:180],
                           observed_s=stamp())
        finally:
            if not stopped.is_set():
                report['receiver_end'] = failure or dict(kind='ended', observed_s=stamp())
                try:
                    incoming.put((stamp(), None), timeout=1)
                except queue.Full:
                    pass
    def send(kind, **fields):
        nonlocal nonce
        nonce += 1
        value = dict(event_id=f'reuse_{nonce}', type=kind, **fields)
        ws.send(json.dumps(value, ensure_ascii=False, separators=(',', ':')))
        record = dict(observed_s=stamp(), type=kind, event_id=value['event_id'])
        with (args.out/'sent.jsonl').open('a', encoding='utf8') as log:
            log.write(json.dumps(record)+'\n')
        with (args.out/'traffic.jsonl').open('a', encoding='utf8') as log:
            data = dict(record, direction='send')
            if kind == 'input_audio_buffer.append':
                data['samples'] = len(base64.b64decode(fields['audio'], validate=True))//2
            log.write(json.dumps(data)+'\n')
    def recv(timeout):
        try:
            at, raw = incoming.get(timeout=timeout)
        except queue.Empty:
            return None, None
        if raw is None:
            raise RuntimeError('Unexpected connection termination')
        if len(raw) > 2*1024*1024:
            raise ValueError('Event budget exceeded')
        event = json.loads(raw)
        with (args.out/'wire.jsonl').open('a', encoding='utf8') as log:
            log.write(raw.replace(key, '[REDACTED]')+'\n')
        record = dict(observed_s=at, event={k:v for k,v in event.items()
            if not (event.get('type') == 'response.audio.delta' and k == 'delta')})
        with (args.out/'events.jsonl').open('a', encoding='utf8') as log:
            log.write(json.dumps(record, ensure_ascii=False).replace(key, '[REDACTED]')+'\n')
        with (args.out/'traffic.jsonl').open('a', encoding='utf8') as log:
            log.write(json.dumps(dict(direction='receive', observed_s=at, event=event),
                                 ensure_ascii=False).replace(key, '[REDACTED]')+'\n')
        if event.get('type') == 'error':
            raise RuntimeError('Provider error retained in events')
        return at, event
    def wait_for(kind, seconds):
        deadline = min(session_deadline, time.monotonic()+seconds)
        while time.monotonic() < deadline:
            at, event = recv(.05)
            if not event:
                continue
            if event.get('type') == kind:
                return at, event
            if event.get('type', '').startswith(('response.', 'conversation.item.input_audio_transcription')):
                raise RuntimeError('Unexpected prior-turn activity at barrier')
        raise TimeoutError(kind)
    try:
        report['attempts'] = 1
        save()
        ws = websocket.create_connection('wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model='+MODEL,
            header=['Authorization: Bearer '+key], timeout=20, redirect_limit=0, enable_multithread=True)
        worker = threading.Thread(target=receiver, daemon=True)
        worker.start()
        _, created = wait_for('session.created', 5)
        report['session_id'] = created.get('session', {}).get('id')
        send('session.update', session=config)
        report['ready_s'], updated = wait_for('session.updated', 5)
        if updated.get('session', {}).get('instructions') != config['instructions']:
            raise ValueError('Wrong session instruction echo')
        actual = updated.get('session', {}).get('audio', {}).get('output', {}).get('format')
        report['output_format_echo'] = actual
        if args.output_rate != 24000 and (not isinstance(actual, dict) or
                actual.get('type') != 'pcm' or actual.get('sample_rate') != args.output_rate):
            raise ValueError('Missing or mismatched output format echo')
        seen_inputs, seen_responses = set(), set()
        for number, (row, pcm, trigger_samples) in enumerate(prepared):
            turn = dict(id=row['id'], sent_samples=0, text='', final_asr=None, done=False,
                        pcm_bytes=0, preview_ids=[], expected_samples=len(pcm)//2,
                        input_pcm_sha256=hashlib.sha256(pcm).hexdigest())
            report['turns'].append(turn)
            turn['start_s'] = stamp()
            offset = 0
            deadline = min(session_deadline, time.monotonic()+35)
            committed = requested = False
            input_id = response_id = None
            audio_name = row['id'] if args.fixture_ids.count(row['id']) == 1 else f'turn-{number+1:02}-{row["id"]}'
            turn['audio_file'] = audio_name+'.wav'
            audio = audios[audio_name] = bytearray()
            while time.monotonic() < deadline:
                at, event = recv(.003)
                if event:
                    kind = event.get('type')
                    if kind == 'input_audio_buffer.committed':
                        ident = event.get('item_id')
                        if not committed or not ident or ident in seen_inputs or input_id:
                            raise ValueError('Missing or reused input identity')
                        input_id = ident
                        seen_inputs.add(ident)
                        turn['commit_ack_s'] = at
                    elif kind == 'conversation.item.input_audio_transcription.delta':
                        ident = event.get('item_id')
                        if not ident or ident in seen_inputs and ident != input_id:
                            raise ValueError('Missing or stale preview identity')
                        if ident not in turn['preview_ids']:
                            if len(turn['preview_ids']) >= 16:
                                raise ValueError('Preview identity limit')
                            turn['preview_ids'].append(ident)
                    elif kind == 'conversation.item.input_audio_transcription.completed':
                        if not input_id or event.get('item_id') != input_id or turn['final_asr'] is not None:
                            raise ValueError('Stale final ASR')
                        turn['final_asr'] = event.get('transcript')
                        turn['asr_item_id'] = event.get('item_id')
                        turn['final_asr_s'] = at
                    elif kind == 'response.created':
                        ident = event.get('response', {}).get('id')
                        if not requested or not ident or ident in seen_responses or response_id:
                            raise ValueError('Missing or reused response identity')
                        response_id = ident
                        seen_responses.add(ident)
                        turn['response_created_s'] = at
                    elif kind == 'response.done':
                        response = event.get('response', {})
                        if turn['done'] or response.get('id') != response_id or response.get('status') != 'completed':
                            raise ValueError('Unmatched or incomplete response')
                        turn['done'] = True
                        turn['done_s'] = at
                    elif kind == 'response.audio.delta':
                        if turn['done'] or event.get('response_id') != response_id:
                            raise ValueError('Stale response audio')
                        data = base64.b64decode(event['delta'], validate=True)
                        if len(data) % 2:
                            raise ValueError('Odd output PCM')
                        turn['pcm_bytes'] += len(data)
                        turn.setdefault('first_pcm_s', at)
                        if turn['pcm_bytes'] > args.output_rate*2*12:
                            raise ValueError('Output audio budget exceeded')
                        audio.extend(data)
                    elif kind in ('response.audio_transcript.delta', 'response.text.delta'):
                        if event.get('response_id') != response_id:
                            raise ValueError('Stale response text')
                        turn['text'] += event.get('delta', '')
                        if len(turn['text'].encode('utf8')) > 2048:
                            raise ValueError('Text budget exceeded')
                    elif kind in ('input_audio_buffer.speech_started', 'input_audio_buffer.speech_stopped'):
                        raise ValueError('Unexpected automatic endpoint')
                if offset < len(pcm) and stamp() >= turn['start_s']+offset/32000:
                    chunk = pcm[offset:offset+args.chunk_samples*2]
                    send('input_audio_buffer.append', audio=base64.b64encode(chunk).decode('ascii'))
                    offset += len(chunk)
                    turn['sent_samples'] = offset//2
                if args.manual_draft and not requested and offset//2 >= trigger_samples:
                    send('response.create')
                    requested = True
                    turn['draft_request_s'] = stamp()
                    turn['draft_input_samples'] = offset//2
                if not committed and offset == len(pcm):
                    send('input_audio_buffer.commit')
                    if not requested:
                        send('response.create')
                        requested = True
                    turn['commit_s'] = stamp()
                    turn['draft_active_or_pending_at_commit'] = args.manual_draft and not turn['done']
                    committed = True
                if turn['done'] and turn['final_asr']:
                    break
                if (args.confirm_missing_final and committed and input_id and turn['done'] and
                        not turn['final_asr'] and 'confirmation_request_s' not in turn and
                        stamp()-turn['commit_s'] >= .35):
                    # Preserve the first candidate; never replay input or use it as final ASR.
                    # A second response tests whether the already committed input can finish
                    # transcription on this same connection. Neither response is played.
                    turn['draft_response'] = {name: turn[name] for name in
                        ('text', 'done', 'pcm_bytes', 'audio_file', 'response_created_s', 'done_s')}
                    if 'first_pcm_s' in turn:
                        turn['draft_response']['first_pcm_s'] = turn.pop('first_pcm_s')
                    turn['draft_response'].update(response_id=response_id,
                        pcm_sha256=hashlib.sha256(audio).hexdigest())
                    audio_name += '-confirmation'
                    turn.update(text='', done=False, pcm_bytes=0, audio_file=audio_name+'.wav')
                    turn.pop('response_created_s')
                    turn.pop('done_s')
                    response_id = None
                    audio = audios[audio_name] = bytearray()
                    send('response.create')
                    turn['confirmation_request_s'] = stamp()
                if (args.observe_missing_final and committed and input_id and turn['done'] and
                        stamp()-turn['commit_s'] >= 8):
                    turn['missing_final_after_8s'] = True
                    break
            turn['input_item_id'], turn['response_id'] = input_id, response_id
            with wave.open(str(args.out/turn['audio_file']), 'wb') as wav:
                wav.setparams((1, 2, args.output_rate, 0, 'NONE', 'not compressed'))
                wav.writeframes(audio)
            turn['pcm_sha256'] = hashlib.sha256(audio).hexdigest()
            turn['complete_input'] = bool(turn['final_asr'] and complete_input(row['text'], turn['final_asr']))
            save()
            turn['protocol_complete'] = bool(turn['done'] and turn['complete_input'] and turn['pcm_bytes'] and
                    input_id == turn.get('asr_item_id') and turn['sent_samples'] == turn['expected_samples'])
            if not turn['protocol_complete'] and not (args.observe_missing_final and input_id and
                    turn['done'] and turn.get('missing_final_after_8s')):
                raise ValueError('Turn did not complete with exact fixture input')
            if not args.manual_draft or number < 2:
                turn['clear_sent_s'] = stamp()
                send('input_audio_buffer.clear')
                turn['clear_ack_s'], _ = wait_for('input_audio_buffer.cleared', 2)
                turn['clear_ms'] = round((turn['clear_ack_s']-turn['clear_sent_s'])*1000, 3)
            save()
        report['complete'] = len(report['turns']) == 3 and all(t['protocol_complete'] for t in report['turns'])
    except Exception as exc:
        report['error_type'] = type(exc).__name__
        report['reason'] = str(exc).replace(key, '[REDACTED]')[:180]
    finally:
        stopped.set()
        if ws is not None:
            ws.close(timeout=2)
        if worker is not None:
            worker.join(timeout=3)
        for ident, audio in audios.items():
            with wave.open(str(args.out/(ident+'.wav')), 'wb') as wav:
                wav.setparams((1, 2, args.output_rate, 0, 'NONE', 'not compressed'))
                wav.writeframes(audio)
        report['finished_s'] = stamp()
        save()
    print(json.dumps(dict(complete=report['complete'], turns=len(report['turns']),
                         error=report.get('reason')), ensure_ascii=False))
    if not report['complete']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
