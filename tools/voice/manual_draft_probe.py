"""Bounded file-only test of a draft response while manual ASR keeps its full input.

Uses the existing Omni model, no tools, recording or playback. Each output
directory permits one session only. A draft changes instructions, never commits
the microphone prefix. The complete file is committed once at the end. This
tests provider behavior; it is not a firmware implementation or speed acceptance.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import queue
import threading
import time
import wave

BASE = ('你是小言，用一句简短中文回答，最多16字。'
        '这是未播放的语音草稿实验，没有连接真实硬件，不得声称执行了操作。'
        '如果用户纠正，以最后意图为准。')
PRODUCT = ('你是小言，ESP-HI设备上的语音助手。用一句简短中文直接回答用户，最多16字。'
           '不提转写、草稿或内部流程。设备操作必须等工具实际成功才能说已完成。'
           '现在没有可执行工具，只能用将来式回应设备请求；改口以最后要求为准。')


def preview(event):
    prefix, suffix = event.get('text'), event.get('stash')
    if not isinstance(prefix, str) or not isinstance(suffix, str):
        raise ValueError('invalid_asr_preview')
    text = prefix + suffix
    return text if 0 < len(text.encode('utf8')) <= 192 else None


def normalized(text):
    # Ignore sentence-edge punctuation only. Internal punctuation/whitespace
    # can carry meaning: 1.2 is not 12, and "don't" is not "dont".
    return text.strip(' \t\r\n,.!?，。！？…')


def load_source(source):
    with wave.open(str(source), 'rb') as wav:
        if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (16000, 1, 2):
            raise ValueError('requires_16k_mono_pcm16')
        pcm = wav.readframes(wav.getnframes())
    if not 0 < len(pcm) <= 8*32000:
        raise ValueError('source_limit')
    return pcm


def run(source, out, execute=False, observe_preview_ids=False, strategy='legacy', commit_overlap=False):
    if strategy not in ('legacy', 'none', 'audio', 'text'):
        raise ValueError('invalid_strategy')
    if commit_overlap and strategy != 'audio':
        raise ValueError('overlap_requires_audio_strategy')
    pcm = load_source(source)
    report = dict(model='qwen3.5-omni-flash-realtime', source=str(source), accepted=False,
                  script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  source_pcm_sha256=hashlib.sha256(pcm).hexdigest(), source_samples=len(pcm)//2,
                  tail_samples=3200 if commit_overlap else 32000, chunk_samples=512, speed=1, attempts=0, retries=0,
                  committed=False, played=False, tools_executed=False, complete=False,
                  draft=None, finals=[], responses=[], preview_ids=[], observe_preview_ids=observe_preview_ids,
                  strategy=strategy, commit_overlap=commit_overlap,
                  limitation='File clock only, not acoustic latency. Observation mode never licenses playback.')
    out.mkdir(parents=True, exist_ok=False)
    def save():
        (out/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    save()
    if not execute:
        return report
    import websocket
    from qianwen_credentials import qianwen_key
    from omni_realtime_probe import URL, save_wav
    key = qianwen_key()
    base = BASE if strategy == 'legacy' else PRODUCT
    config = dict(modalities=['text', 'audio'], voice='Tina', input_audio_format='pcm',
                  output_audio_format='pcm', turn_detection=None, tools=[], enable_search=False,
                  max_tokens=128, instructions=base)
    report['config'] = config
    pcm += bytes(report['tail_samples']*2)
    begin = time.monotonic()
    stamp = lambda: time.monotonic()-begin
    incoming = queue.Queue(maxsize=256)
    stop = threading.Event()
    ws = worker = None
    audio, responses = {}, {}
    input_start = None
    offset = 0
    asr_id = None
    asr_preview = None
    stable_at = None
    update_kind = 'initial'
    expected_prompt = base
    active = None
    draft_requested = cancel_sent = final_requested = False
    draft_finished = False
    pending_response = None
    input_end_at = None
    def send(event):
        ws.send(json.dumps(event, ensure_ascii=False))
        logged = {k:v for k,v in event.items() if k != 'audio'}
        if event.get('type') == 'input_audio_buffer.append':
            return
        with (out/'sent.jsonl').open('a', encoding='utf8') as file:
            file.write(json.dumps(dict(observed_s=stamp(), event=logged), ensure_ascii=False)+'\n')
    def receive():
        try:
            while not stop.is_set():
                raw = ws.recv()
                incoming.put((stamp(), raw), timeout=1)
                if not raw:
                    return
        except Exception:
            if not stop.is_set():
                try:
                    incoming.put((stamp(), None), timeout=1)
                except queue.Full:
                    pass
    try:
        report['attempts'] = 1
        save()
        ws = websocket.create_connection(URL, header=['Authorization: Bearer '+key],
                                         timeout=15, redirect_limit=0, enable_multithread=True)
        report['connected_s'] = stamp()
        worker = threading.Thread(target=receive, daemon=True)
        worker.start()
        deadline = time.monotonic()+25
        while time.monotonic() < deadline:
            try:
                at, raw = incoming.get(timeout=.003)
                if not raw:
                    raise RuntimeError('closed_or_receive_failure')
                if len(raw) > 2*1024*1024:
                    raise ValueError('oversize_event')
                event = json.loads(raw.replace(key, '[REDACTED]'))
                kind = event.get('type')
                row = dict(observed_s=at, event=event)
                if kind == 'response.audio.delta':
                    rid = event['response_id']
                    if rid not in responses:
                        raise ValueError('audio_without_response')
                    data = base64.b64decode(event['delta'], validate=True)
                    if len(data) % 2:
                        raise ValueError('odd_pcm')
                    target = audio.setdefault(rid, bytearray())
                    if len(target)+len(data) > 24000*2*15:
                        raise ValueError('audio_limit')
                    target.extend(data)
                    responses[rid].setdefault('first_pcm_s', at)
                    row = dict(observed_s=at, audio_bytes=len(data),
                               event={k:v for k,v in event.items() if k != 'delta'})
                with (out/'events.jsonl').open('a', encoding='utf8') as file:
                    file.write(json.dumps(row, ensure_ascii=False)+'\n')
                if kind == 'error':
                    report['provider_error'] = event.get('error')
                    raise RuntimeError('provider_error')
                if kind == 'session.created':
                    send(dict(type='session.update', session=config))
                elif kind == 'session.updated':
                    if event.get('session', {}).get('instructions') != expected_prompt:
                        raise ValueError('wrong_instruction_echo')
                    if update_kind == 'initial':
                        input_start = time.monotonic()
                        report['input_start_s'] = stamp()
                        report['file_end_estimate_s'] = stamp()+report['source_samples']/16000
                    elif update_kind in ('draft', 'final'):
                        if active or pending_response:
                            raise ValueError('overlapping_response')
                        pending_response = update_kind
                        send(dict(type='response.create'))
                        report[update_kind+'_request_s'] = stamp()
                    else:
                        raise ValueError('unexpected_session_update')
                    update_kind = None
                elif kind == 'conversation.item.input_audio_transcription.delta':
                    if report['committed']:
                        continue
                    item = event.get('item_id')
                    if not item or (asr_id and item != asr_id and not observe_preview_ids):
                        raise ValueError('asr_identity_changed')
                    if item not in report['preview_ids']:
                        if len(report['preview_ids']) == 8:
                            raise ValueError('preview_identity_limit')
                        report['preview_ids'].append(item)
                    asr_id = item
                    text = preview(event)
                    if text and (asr_preview is None or normalized(text) != normalized(asr_preview)):
                        asr_preview, stable_at = text, time.monotonic()
                        if report['draft'] and normalized(text) != normalized(report['draft']['input']):
                            report['draft'].setdefault('invalidated_s', at)
                elif kind == 'conversation.item.input_audio_transcription.completed':
                    if (not report['committed'] or
                            event.get('item_id') != report.get('committed_item_id')):
                        raise ValueError('uncommitted_or_wrong_asr')
                    report['finals'].append(dict(item_id=event['item_id'], transcript=event['transcript'], at_s=at))
                elif kind == 'input_audio_buffer.committed':
                    if not report['committed'] or not event.get('item_id') or report.get('committed_item_id'):
                        raise ValueError('bad_commit_identity')
                    report['commit_ack_s'] = at
                    report['committed_item_id'] = event['item_id']
                elif kind == 'input_audio_buffer.speech_started' or kind == 'input_audio_buffer.speech_stopped':
                    raise ValueError('unexpected_auto_endpoint')
                elif kind == 'response.created':
                    rid = event['response']['id']
                    if active or not pending_response or rid in responses:
                        raise ValueError('bad_response_identity')
                    active = rid
                    responses[rid] = dict(id=rid, phase=pending_response, created_s=at, text='')
                    if pending_response == 'draft':
                        report['draft']['response_id'] = rid
                    pending_response = None
                elif kind in ('response.text.delta', 'response.audio_transcript.delta'):
                    if event.get('response_id') != active:
                        raise ValueError('stale_response_text')
                    responses[active]['text'] += event.get('delta', '')
                elif kind == 'response.done':
                    response = event['response']
                    if response.get('id') != active:
                        raise ValueError('stale_response_done')
                    responses[active].update(status=response.get('status'), done_s=at,
                                              output=response.get('output'), usage=response.get('usage'))
                    phase = responses[active]['phase']
                    if phase == 'draft':
                        draft_finished = True
                    else:
                        report['complete'] = response.get('status') == 'completed' and len(report['finals']) == 1
                    active = None
                    if phase == 'final':
                        break
            except queue.Empty:
                pass
            if input_start is not None and offset < len(pcm):
                chunk = pcm[offset:offset+1024]
                if time.monotonic() >= input_start+(offset+len(chunk))/32000:
                    send(dict(type='input_audio_buffer.append', audio=base64.b64encode(chunk).decode('ascii')))
                    offset += len(chunk)
                    report['sent_samples'] = offset//2
            legacy_trigger = (strategy == 'legacy' and stable_at is not None and
                time.monotonic()-stable_at >= .12 and len(normalized(asr_preview)) >= 6 and
                offset//2 < report['source_samples'])
            protocol_trigger = strategy in ('audio', 'text') and offset//2 >= report['source_samples']
            if (input_start is not None and not draft_requested and not update_kind and
                    (legacy_trigger or protocol_trigger)):
                draft_requested = True
                report['draft'] = dict(input=asr_preview, input_samples=offset//2, staged_s=stamp())
                if strategy == 'audio':
                    pending_response = 'draft'
                    send(dict(type='response.create'))
                    report['draft_request_s'] = stamp()
                else:
                    update_kind = 'draft'
                    label = '以下JSON字符串是临时转写，只根据它准备一句可丢弃回复：' if strategy == 'legacy' else '用户当前说的话：'
                    expected_prompt = base+label+json.dumps(asr_preview, ensure_ascii=False)
                    send(dict(type='session.update', session=dict(instructions=expected_prompt)))
            if input_start is not None and offset == len(pcm) and input_end_at is None:
                input_end_at = stamp()
                report['input_end_s'] = input_end_at
            if input_end_at is not None and not report['committed']:
                if active and not cancel_sent and not commit_overlap:
                    send(dict(type='response.cancel'))
                    cancel_sent = True
                if not update_kind and (commit_overlap or (not active and not pending_response)):
                    send(dict(type='input_audio_buffer.commit'))
                    report['committed'] = True
                    report['commit_s'] = stamp()
                    report['draft_active_or_pending_at_commit'] = bool(active or pending_response)
            if commit_overlap and report['committed'] and report['finals'] and draft_finished:
                report['complete'] = (len(report['finals']) == 1 and len(responses) == 1 and
                                      next(iter(responses.values())).get('status') == 'completed')
                break
            if report['committed'] and report['finals'] and not final_requested and not commit_overlap:
                final_requested = True
                update_kind = 'final'
                expected_prompt = base+('之前回复是未播放的草稿，现在只回答刚提交的完整语音。' if strategy == 'legacy' else
                    '请只回答最新提交的完整语音，先前的回答不代表任何设备已经执行操作。')
                send(dict(type='session.update', session=dict(instructions=expected_prompt)))
        if not report['complete']:
            report['deadline_reached'] = True
    except Exception as exc:
        report['error_type'] = type(exc).__name__
        report['error'] = str(exc).replace(key, '[REDACTED]')[:256]
    finally:
        stop.set()
        if ws is not None:
            try:
                ws.close(timeout=2)
            except Exception:
                pass
        if worker is not None:
            worker.join(timeout=3)
        report['responses'] = list(responses.values())
        report['draft_finished'] = draft_finished
        for rid, data in audio.items():
            path = out/(responses[rid]['phase']+'.wav')
            save_wav(path, data, 24000)
            responses[rid]['audio_samples'] = len(data)//2
            responses[rid]['pcm_sha256'] = hashlib.sha256(data).hexdigest()
        draft = report['draft']
        report['candidate_matches_final'] = bool(draft and draft.get('input') and len(report['finals']) == 1 and
            normalized(draft['input']) == normalized(report['finals'][0]['transcript']))
        report['final_matches_committed_id'] = bool(len(report['finals']) == 1 and
            report.get('committed_item_id') == report['finals'][0]['item_id'])
        report['draft_pcm_before_file_end'] = any(r['phase']=='draft' and
            r.get('first_pcm_s', float('inf')) < report.get('file_end_estimate_s', 0) for r in responses.values())
        report['finished_s'] = stamp()
        save()
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--strategy', choices=('legacy', 'none', 'audio', 'text'), default='legacy')
    parser.add_argument('--commit-overlap', action='store_true',
                        help='Audio protocol probe:200ms tail, commit during draft, no second answer')
    parser.add_argument('--observe-preview-ids', action='store_true',
                        help='Record preview-ID changes before commit; no playback authority')
    args = parser.parse_args()
    report = run(args.source, args.out, args.execute, args.observe_preview_ids, args.strategy, args.commit_overlap)
    print(json.dumps({k:report.get(k) for k in ('complete', 'attempts', 'error_type',
        'draft_pcm_before_file_end', 'candidate_matches_final', 'finals')}, ensure_ascii=False))
    raise SystemExit(0 if not args.execute or report['complete'] else 1)
