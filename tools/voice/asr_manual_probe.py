"""One bounded standalone-ASR session; existing synthetic files, no playback or tools."""
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

MODEL = 'qwen3-asr-flash-realtime'
URL = 'wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model='+MODEL
CONFIG = dict(input_audio_format='pcm', sample_rate=16000, turn_detection=None)


def created_item_kind(event, input_id):
    """An ID-less in-progress announcement carries no input authority."""
    item = event.get('item', {})
    content = item.get('content')
    if item.get('type') != 'message' or not isinstance(content, list) or len(content) != 1:
        raise ValueError('Invalid ASR conversation item')
    if content[0].get('type') != 'input_audio':
        raise ValueError('Non-audio ASR conversation item')
    if (not input_id and 'id' not in item and item.get('status') == 'in_progress'
            and item.get('role') == 'assistant' and content[0].get('transcript') is None):
        return 'provisional'
    if not input_id or item.get('id') != input_id or item.get('role') != 'user':
        raise ValueError('Unexpected conversation item')
    return 'committed'


def run(out, execute=False, observe_unbound=False):
    out.mkdir(parents=True, exist_ok=False)
    script = Path(__file__).read_bytes()
    (out/'executed-script.py').write_bytes(script)
    pool = {}
    for name in ('artifacts/voice-cloud/prompts-01/manifest.json',
                 'artifacts/voice-fast/prefetch-fixtures-01/manifest.json'):
        manifest = json.loads(Path(name).read_text(encoding='utf-8-sig'))
        assert manifest['complete'] and manifest['synthetic']
        pool.update({row['id']: row for row in manifest['prompts']})
    cases, fixtures = [], []
    for number, ident in enumerate(('greeting', 'correction', 'greeting'), 1):
        row = pool[ident]
        source = Path(row['path'])
        original = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(source), '-ar', '16000',
            '-ac', '1', '-f', 's16le', '-'], check=True, capture_output=True, timeout=15).stdout
        if not original or len(original) % 2 or len(original) > 8*32000:
            raise ValueError('Invalid fixture')
        pcm = bytes(400*32)+original+bytes(700*32)
        filename = f'input-{number:02}.wav'
        with wave.open(str(out/filename), 'wb') as wav:
            wav.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
            wav.writeframes(pcm)
        fixture = dict(id=ident, expected=row['text'], samples=len(pcm)//2,
            source=str(source), source_file_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
            original_samples=len(original)//2, input_file=filename,
            pcm_sha256=hashlib.sha256(pcm).hexdigest())
        fixtures.append(fixture)
        cases.append(pcm)
    report = dict(model=MODEL, url=URL, config=CONFIG, accepted=False,
        scope='standalone_host_asr_only', execute=execute, attempts=0, retries=0,
        complete=False, played=False, tools_executed=False, chunk_samples=464,
        observe_unbound=observe_unbound,
        lead_ms=400, tail_ms=700, fixtures=fixtures, turns=[],
        script_sha256=hashlib.sha256(script).hexdigest())

    def save():
        (out/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf8')

    save()
    if not execute:
        return report
    import websocket
    from qianwen_credentials import qianwen_key
    key = qianwen_key()
    began = time.monotonic()
    absolute = began+120
    stamp = lambda: round(time.monotonic()-began, 6)
    stop = threading.Event()
    pending = queue.Queue(maxsize=256)
    ws = worker = None
    nonce = 0

    def receive():
        try:
            while not stop.is_set():
                raw = ws.recv()
                pending.put((stamp(), raw), timeout=2)
                if not raw:
                    return
        except Exception as exc:
            if not stop.is_set():
                report['receiver_error'] = type(exc).__name__
                try:
                    pending.put((stamp(), None), timeout=1)
                except queue.Full:
                    pass

    def send(kind, **fields):
        nonlocal nonce
        if time.monotonic() >= absolute:
            raise TimeoutError('Session budget')
        nonce += 1
        message = dict(type=kind, event_id=f'asr_probe_{nonce}', **fields)
        ws.send(json.dumps(message, ensure_ascii=False, separators=(',', ':')))
        meta = dict(type=kind, event_id=message['event_id'])
        if 'audio' in fields:
            pcm = base64.b64decode(fields['audio'], validate=True)
            meta.update(samples=len(pcm)//2, pcm_sha256=hashlib.sha256(pcm).hexdigest())
        else:
            meta.update(fields)
        with (out/'traffic.jsonl').open('a', encoding='utf8') as log:
            log.write(json.dumps(dict(direction='send', observed_s=stamp(), event=meta), ensure_ascii=False)+'\n')

    def recv(timeout):
        try:
            at, raw = pending.get(timeout=timeout)
        except queue.Empty:
            return None, None
        if not raw:
            raise RuntimeError('Unexpected connection end')
        if len(raw) > 6144:
            raise ValueError('ASR event exceeds device metadata limit')
        event = json.loads(raw)
        with (out/'wire.jsonl').open('a', encoding='utf8') as log:
            log.write(raw.replace(key, '[REDACTED]')+'\n')
        with (out/'traffic.jsonl').open('a', encoding='utf8') as log:
            log.write(json.dumps(dict(direction='receive', observed_s=at, event=event),
                                ensure_ascii=False).replace(key, '[REDACTED]')+'\n')
        if event.get('type') in ('error', 'conversation.item.input_audio_transcription.failed'):
            raise RuntimeError('Provider error retained in wire log')
        return at, event

    def wait(kind, seconds):
        end = min(absolute, time.monotonic()+seconds)
        while time.monotonic() < end:
            at, event = recv(.02)
            if event is None:
                continue
            if event.get('type') != kind:
                raise ValueError('Unexpected event at '+kind+' barrier')
            return at, event
        raise TimeoutError(kind)

    try:
        report['attempts'] = 1
        save()
        ws = websocket.create_connection(URL, header=['Authorization: Bearer '+key],
            timeout=15, redirect_limit=0, enable_multithread=True)
        worker = threading.Thread(target=receive, daemon=True)
        worker.start()
        _, created = wait('session.created', 5)
        report['created'] = created['session']
        if created['session'].get('model') != MODEL:
            raise ValueError('Wrong model echo')
        send('session.update', session=CONFIG)
        report['ready_s'], updated = wait('session.updated', 5)
        report['updated'] = updated['session']
        if updated['session'].get('input_audio_format') != 'pcm' or updated['session'].get('turn_detection'):
            raise ValueError('Wrong manual PCM echo')
        if updated['session'].get('sample_rate', 16000) != 16000:
            raise ValueError('Wrong sample rate echo')
        seen = set()
        for fixture, pcm in zip(fixtures, cases):
            turn = dict(id=fixture['id'], start_s=stamp(), sent_samples=0, previews=[],
                        final_asr=None, protocol_complete=False)
            report['turns'].append(turn)
            offset = 0
            committed = False
            input_id = None
            deadline = min(absolute, time.monotonic()+25)
            while time.monotonic() < deadline:
                at, event = recv(.003)
                if event:
                    kind, ident = event.get('type'), event.get('item_id')
                    if kind == 'input_audio_buffer.committed':
                        if not committed or 'commit_ack_s' in turn or ident in seen:
                            raise ValueError('Invalid commit identity')
                        if not ident and not observe_unbound:
                            raise ValueError('Missing commit identity')
                        input_id = ident
                        if ident:
                            seen.add(ident)
                        turn.update(input_item_id=ident, commit_ack_s=at)
                    elif kind == 'conversation.item.input_audio_transcription.text':
                        if (not ident and not observe_unbound) or ident in seen and ident != input_id:
                            raise ValueError('Stale preview identity')
                        if input_id and ident and ident != input_id:
                            raise ValueError('Mismatched preview identity')
                        text, stash = event.get('text'), event.get('stash')
                        if not isinstance(text, str) or not isinstance(stash, str):
                            raise ValueError('Missing preview fields')
                        if len((text+stash).encode('utf8')) > 2048 or len(turn['previews']) >= 128:
                            raise ValueError('Preview budget')
                        turn['previews'].append(dict(at_s=at, item_id=ident, text=text+stash,
                            uploaded_samples=offset//2, language=event.get('language')))
                    elif kind == 'conversation.item.input_audio_transcription.completed':
                        if not committed or 'commit_ack_s' not in turn or turn['final_asr'] is not None:
                            raise ValueError('Final before full commit acknowledgement or duplicate final')
                        if ((not input_id or not ident) and not observe_unbound) or (input_id and ident and ident != input_id):
                            raise ValueError('Unmatched final ASR')
                        text = event.get('transcript')
                        if not isinstance(text, str) or not text or len(text.encode('utf8')) > 2048:
                            raise ValueError('Invalid final ASR')
                        turn.update(final_asr=text, final_asr_s=at, language=event.get('language'),
                                    final_item_id=ident, identity_verified=bool(input_id and ident == input_id))
                    elif kind == 'conversation.item.created':
                        # Observer retains ID-less announcements even after a commit;
                        # they still cannot make identity_verified or protocol_complete true.
                        checked_id = None if observe_unbound and 'id' not in event.get('item', {}) else input_id
                        form = created_item_kind(event, checked_id)
                        if form == 'provisional':
                            if not offset or turn.get('provisional_items', 0) >= 3:
                                raise ValueError('Unexpected provisional item count')
                            turn['provisional_items'] = turn.get('provisional_items', 0)+1
                    else:
                        raise ValueError('Unexpected ASR event '+str(kind))
                if offset < len(pcm) and stamp() >= turn['start_s']+offset/32000:
                    chunk = pcm[offset:offset+464*2]
                    send('input_audio_buffer.append', audio=base64.b64encode(chunk).decode('ascii'))
                    offset += len(chunk)
                    turn['sent_samples'] = offset//2
                if offset == len(pcm) and not committed:
                    send('input_audio_buffer.commit')
                    turn['commit_s'] = stamp()
                    committed = True
                if turn['final_asr']:
                    break
                if committed and stamp()-turn['commit_s'] >= 8:
                    raise TimeoutError('No final ASR within8s')
            turn['complete_input'] = bool(turn['final_asr'] and complete_input(fixture['expected'], turn['final_asr']))
            turn['protocol_complete'] = bool(turn.get('identity_verified') and turn['complete_input'] and
                                             turn['sent_samples'] == fixture['samples'])
            save()
            if not turn['final_asr']:
                raise TimeoutError('No final ASR')
        send('session.finish')
        report['server_finished_s'], _ = wait('session.finished', 5)
        report['transcripts_complete'] = all(turn['complete_input'] for turn in report['turns']) and len(report['turns']) == 3
        report['complete'] = all(turn['protocol_complete'] for turn in report['turns']) and len(report['turns']) == 3
    except Exception as exc:
        report['error_type'] = type(exc).__name__
        report['reason'] = str(exc).replace(key, '[REDACTED]')[:180]
    finally:
        stop.set()
        if ws is not None:
            ws.close(timeout=2)
        if worker is not None:
            worker.join(timeout=3)
        report['finished_s'] = stamp()
        save()
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--observe-unbound', action='store_true',
                        help='Retain missing IDs as unknown associations; never waive protocol acceptance')
    args = parser.parse_args()
    result = run(args.out, args.execute, args.observe_unbound)
    print(json.dumps(dict(complete=result['complete'], attempts=result['attempts'],
                         turns=len(result['turns']), error=result.get('reason')), ensure_ascii=False))
    if args.execute and not result['complete']:
        raise SystemExit(1)
