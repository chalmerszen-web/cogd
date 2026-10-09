"""One host-only Qwen3.5 Omni session; two existing synthetic clips and one mock tool.

The model, voice, formats and tool schema were checked against the official
Model Studio client-events, server-events and omni-realtime-python-sdk pages.
This probe deliberately uses manual commit so VAD delay is not conflated with
model response delay. It does not establish a device/acoustic latency claim.

Never records a microphone, controls hardware, retries, or reconnects. An existing
output directory is an execution receipt and cannot be reused for another run.
"""
import argparse
from array import array
import base64
import hashlib
import json
import math
from pathlib import Path
import queue
import shutil
import subprocess
import threading
import time
import uuid
import wave

import websocket
from qianwen_credentials import qianwen_key

MODEL = 'qwen3.5-omni-flash-realtime'
URL = 'wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model=' + MODEL
TOOL = {'type': 'function', 'function': {
    'name': 'device_light_set_rgb',
    'description': 'Host-only simulated four-LED color setting; no physical device is connected.',
    'parameters': {'type': 'object', 'properties': {
        channel: {'type': 'integer', 'minimum': 0, 'maximum': 255} for channel in 'rgb'
    }, 'required': list('rgb'), 'additionalProperties': False}}}


def save_wav(path, pcm, rate):
    if len(pcm) % 2:
        raise ValueError('odd_pcm_length')
    with wave.open(str(path), 'wb') as output:
        output.setparams((1, 2, rate, 0, 'NONE', 'not compressed'))
        output.writeframes(pcm)


def prepare_source(item, out):
    source = Path(item['path'])
    result = subprocess.run([shutil.which('ffmpeg') or 'ffmpeg', '-v', 'error', '-i', str(source),
        '-ac', '1', '-ar', '16000', '-f', 's16le', '-acodec', 'pcm_s16le', '-'],
        capture_output=True, check=True, timeout=20)
    pcm = result.stdout
    if not pcm or len(pcm) % 2 or len(pcm) > 320000:
        raise ValueError('invalid_bounded_input')
    samples = array('h'); samples.frombytes(pcm)
    energies = [math.sqrt(sum(v*v for v in samples[i:i+160]) / len(samples[i:i+160]))
                for i in range(0, len(samples), 160)]
    threshold = max(64, max(energies) * .02)
    active = [i for i, energy in enumerate(energies) if energy >= threshold]
    if not active:
        raise ValueError('no_active_input')
    first, last = active[0] * 160, min(len(samples), (active[-1] + 1) * 160)
    begin, end = max(0, first - 1600), min(len(samples), last + 1280)
    trimmed = pcm[2*begin:2*end]
    target = out / (item['id'] + '-input.wav')
    save_wav(target, trimmed, 16000)
    facts = {'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
             'text': item['text'], 'synthetic': True, 'converted_samples': len(samples),
             'trim_begin_sample': begin, 'trim_end_sample': end,
             'active_begin_sample': first, 'active_end_sample': last,
             'active_end_after_trim_s': (last-begin)/16000,
             'input_samples': len(trimmed)//2, 'input_duration_s': len(trimmed)/32000,
             'input_pcm_sha256': hashlib.sha256(trimmed).hexdigest(), 'prepared_wav': str(target),
             'trim_method': '10 ms RMS >= max(64, 2% of maximum RMS); retain 100 ms lead/80 ms tail',
             'threshold_rms': threshold,
             'endpoint_note': 'Thresholded synthetic-source endpoint, not microphone/physical acoustic timing.'}
    return trimmed, facts


def run(out, manifest):
    out.mkdir(parents=True, exist_ok=False)
    report = {'scope': 'host_mock', 'model': MODEL, 'endpoint': URL, 'complete': False,
              'attempts': 0, 'retries': 0, 'reconnections': 0, 'audio_inputs': 0,
              'tool_result_continuations': 0, 'max_json_event_bytes': 0,
              'max_audio_delta_chars': 0, 'max_audio_delta_pcm_bytes': 0, 'trials': [],
              'timing_note': 'All times are host monotonic receipt/send times. Input endpoint is estimated from a synthetic WAV; no speaker or device measurements.'}
    def save():
        (out/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    save()
    start = time.monotonic()
    stamp = lambda: round(time.monotonic()-start, 6)
    ws = None; receiver = None; stop = threading.Event(); incoming = queue.Queue(maxsize=256)
    key = None; current = None; audio = {}; calls = {}; done = []
    def send(event):
        event = dict(event, event_id='host_' + uuid.uuid4().hex)
        ws.send(json.dumps(event, ensure_ascii=False, separators=(',', ':')))
        return stamp()
    def receive():
        try:
            while not stop.is_set():
                opcode, packet = ws.recv_data(control_frame=True)
                if opcode in (websocket.ABNF.OPCODE_PING, websocket.ABNF.OPCODE_PONG):
                    continue
                incoming.put((stamp(), opcode, packet), timeout=5)
                if opcode == websocket.ABNF.OPCODE_CLOSE:
                    break
        except Exception as exc:
            if not stop.is_set():
                try: incoming.put((stamp(), -1, type(exc).__name__), timeout=1)
                except queue.Full: pass
    def consume(timeout=.1):
        try: observed, opcode, raw = incoming.get(timeout=max(0, timeout))
        except queue.Empty: return None
        if opcode == -1: raise RuntimeError('receiver_failed')
        if opcode == websocket.ABNF.OPCODE_CLOSE:
            report['close_code'] = int.from_bytes(raw[:2], 'big') if len(raw) >= 2 else None
            raise RuntimeError('unexpected_close')
        if opcode != websocket.ABNF.OPCODE_TEXT:
            raise ValueError('unexpected_binary_event')
        raw = raw.decode('utf8') if isinstance(raw, bytes) else raw
        length = len(raw.encode('utf8'))
        if length > 2*1024*1024: raise ValueError('oversize_event')
        report['max_json_event_bytes'] = max(report['max_json_event_bytes'], length)
        event = json.loads(raw); kind = event.get('type'); rid = event.get('response_id')
        # Never persist credential material, even if echoed by a remote error.
        event = json.loads(json.dumps(event, ensure_ascii=False).replace(key, '[REDACTED]'))
        record = {'observed_s': observed, 'trial': current['id'] if current else None,
                  'json_event_bytes': length, 'json_key_order': list(event), 'event': event}
        if kind == 'response.audio.delta':
            delta = event.get('delta', '')
            data = base64.b64decode(delta, validate=True)
            if not data or len(data) % 2:
                raise ValueError('invalid_audio_delta')
            order = list(event)
            record['type_and_response_id_before_delta'] = all(
                key in order and order.index(key) < order.index('delta') for key in ('type', 'response_id'))
            record['metadata_bytes_with_empty_delta'] = len(json.dumps(
                dict(event, delta=''), ensure_ascii=False, separators=(',', ':')).encode('utf8'))
            if current is None: raise ValueError('audio_outside_trial')
            if len(audio.setdefault(rid, bytearray()))+len(data) > 48000*30:
                raise ValueError('audio_budget_exceeded')
            before = len(audio[rid])
            audio[rid].extend(data)
            values = array('h'); values.frombytes(data)
            active = next((i for i, value in enumerate(values) if abs(value) >= 64), None)
            if active is not None and 'first_signal_pcm_s' not in current:
                current['first_signal_pcm_s'] = observed
                current['first_signal_pcm_response_id'] = rid
                current['first_signal_pcm_sample'] = before//2 + active
            if active is not None and current.get('mock_tool_result'):
                current.setdefault('first_post_tool_signal_pcm_s', observed)
            report['max_audio_delta_chars'] = max(report['max_audio_delta_chars'], len(delta))
            report['max_audio_delta_pcm_bytes'] = max(report['max_audio_delta_pcm_bytes'], len(data))
            current.setdefault('first_audio_s', observed)
            current.setdefault('response_first_audio_s', {}).setdefault(rid, observed)
            record['event'] = {k:v for k,v in event.items() if k != 'delta'}
            record['audio_delta_chars'] = len(delta); record['audio_pcm_bytes'] = len(data)
        elif kind in ('response.text.delta', 'response.audio_transcript.delta') and event.get('delta'):
            if current is not None:
                current.setdefault('first_text_s', observed)
                current.setdefault('texts', {}).setdefault(rid, '')
                current['texts'][rid] += event['delta']
        elif kind == 'conversation.item.input_audio_transcription.completed' and current is not None:
            current['recognized_text'] = event.get('transcript')
        elif kind == 'response.function_call_arguments.done':
            if current is None: raise ValueError('tool_outside_trial')
            calls[event['call_id']] = event
            current.setdefault('function_calls', []).append({'observed_s': observed, 'event': event})
        elif kind == 'response.done':
            done.append((observed, event))
            if current is not None: current.setdefault('responses', []).append({'observed_s': observed, 'event': event})
        with (out/'events.jsonl').open('a', encoding='utf8') as log:
            log.write(json.dumps(record, ensure_ascii=False)+'\n')
        if kind == 'session.updated':
            report['server_session'] = event.get('session')
        if kind == 'error':
            report['server_error'] = event
            raise RuntimeError('server_error')
        return kind
    def wait_for(kind, seconds):
        deadline = time.monotonic()+seconds
        while time.monotonic() < deadline:
            if consume(min(.2, deadline-time.monotonic())) == kind: return
        raise TimeoutError('event_deadline')
    try:
        data = json.loads(manifest.read_text(encoding='utf8'))
        if not data.get('synthetic') or not data.get('complete'):
            raise ValueError('only_existing_complete_synthetic_sources')
        by_id = {row['id']:row for row in data['prompts']}
        sources = [(name, *prepare_source(by_id[name], out)) for name in ('greeting', 'blue')]
        key = qianwen_key()
        report['attempts'] = 1; report['connect_start_s'] = stamp(); save()
        ws = websocket.create_connection(URL, header=['Authorization: Bearer '+key],
                                         timeout=30, redirect_limit=0, enable_multithread=True)
        report['connected_s'] = stamp()
        receiver = threading.Thread(target=receive, daemon=True); receiver.start()
        wait_for('session.created', 15)
        config = {'modalities': ['text', 'audio'], 'voice': 'Tina',
                  'input_audio_format': 'pcm', 'output_audio_format': 'pcm',
                  'turn_detection': None, 'enable_search': False, 'tools': [TOOL],
                  'instructions': '你是小言，简短中文回答，最多一句话。这是电脑上的模拟测试，没有连接真实硬件。介绍自己时直接回答；用户请求蓝灯时调用device_light_set_rgb。工具返回host_mock时说明已模拟设为蓝色，实际设备没有改变，不要重复调用。'}
        report['session_config'] = config
        report['session_update_s'] = send({'type': 'session.update', 'session': config})
        wait_for('session.updated', 15); report['session_ready_s'] = stamp(); save()
        for name, pcm, facts in sources:
            current = {'id': name, 'source': facts, 'complete': False, 'mode': 'host_mock', 'input_chunks': 0}
            report['trials'].append(current); report['audio_inputs'] += 1
            done.clear(); calls.clear(); audio.clear(); save()
            began = time.monotonic(); current['input_start_s'] = stamp()
            current['last_non_silent_input_s'] = current['input_start_s']+facts['active_end_after_trim_s']
            for offset in range(0, len(pcm), 640):
                chunk = pcm[offset:offset+640]
                # Deliver each 20 ms frame when a live capture would have completed it.
                target = began+(offset+len(chunk))/32000
                while time.monotonic() < target:
                    consume(min(.01, target-time.monotonic()))
                at = send({'type': 'input_audio_buffer.append', 'audio': base64.b64encode(chunk).decode('ascii')})
                current['input_chunks'] += 1
                endpoint = facts['active_end_after_trim_s']*32000
                if offset < endpoint <= offset+len(chunk): current['last_non_silent_frame_sent_s'] = at
                while not incoming.empty(): consume(0)
            current['all_input_sent_s'] = stamp()
            current['commit_s'] = send({'type': 'input_audio_buffer.commit'})
            current['response_create_s'] = send({'type': 'response.create'}); save()
            wait_for('response.done', 45)
            if done[-1][1].get('response', {}).get('status') != 'completed':
                raise RuntimeError('response_not_completed')
            if calls:
                if name != 'blue' or len(calls) != 1 or report['tool_result_continuations']:
                    raise ValueError('tool_probe_budget_exceeded')
                call = next(iter(calls.values()))
                arguments = json.loads(call['arguments'])
                if call.get('name') != 'device_light_set_rgb' or set(arguments) != set('rgb') or any(
                    type(arguments[c]) is not int or not 0 <= arguments[c] <= 255 for c in 'rgb'):
                    raise ValueError('invalid_tool_call')
                current['pre_tool_audio_bytes'] = sum(map(len, audio.values()))
                result = dict(ok=True, host_mock=True, device_changed=False, **arguments)
                current['mock_tool_result'] = result
                current['tool_result_sent_s'] = send({'type': 'conversation.item.create', 'item': {
                    'type': 'function_call_output', 'call_id': call['call_id'],
                    'output': json.dumps(result, separators=(',', ':'))}})
                report['tool_result_continuations'] += 1; calls.clear()
                current['tool_continuation_s'] = send({'type': 'response.create'}); save()
                wait_for('response.done', 45)
                if calls or done[-1][1].get('response', {}).get('status') != 'completed':
                    raise RuntimeError('tool_continuation_not_completed')
            current['complete'] = True; current['completed_s'] = stamp()
            current['useful_audio_observation'] = ('mock-tool confirmation: first nontrivial PCM after mock result'
                if current.get('mock_tool_result') else 'greeting reply: first nontrivial PCM; semantic transcript review required')
            useful = current.get('first_post_tool_signal_pcm_s') if current.get('mock_tool_result') else current.get('first_signal_pcm_s')
            if useful is not None:
                current['first_useful_pcm_s'] = useful
                current['first_useful_pcm_after_input_s'] = useful-current['last_non_silent_input_s']
                current['first_useful_pcm_after_commit_s'] = useful-current['commit_s']
            current['mode'] = 'host_mock_function' if current.get('mock_tool_result') else 'host_audio_reply'
            for metric in ('first_text_s', 'first_audio_s', 'first_signal_pcm_s', 'first_post_tool_signal_pcm_s'):
                if metric in current:
                    current[metric[:-2]+'_after_input_s'] = current[metric]-current['last_non_silent_input_s']
                    current[metric[:-2]+'_after_commit_s'] = current[metric]-current['commit_s']
            current['outputs'] = []
            for index, (rid, pcm_out) in enumerate(audio.items()):
                target = out/f'{name}-response-{index+1}.wav'; save_wav(target, pcm_out, 24000)
                current['outputs'].append({'response_id': rid, 'path': str(target),
                    'samples': len(pcm_out)//2, 'seconds': len(pcm_out)/48000,
                    'pcm_sha256': hashlib.sha256(pcm_out).hexdigest()})
            save(); current = None; audio.clear()
        report['complete'] = True
    except Exception as exc:
        report['error_type'] = type(exc).__name__
        # No exception string: a WebSocket failure may contain request headers.
        status = getattr(exc, 'status_code', None)
        if isinstance(status, int): report['http_status'] = status
    finally:
        if current is not None and audio:
            for index, (rid, pcm_out) in enumerate(audio.items()):
                if len(pcm_out) % 2 == 0: save_wav(out/f'{current["id"]}-partial-{index+1}.wav', pcm_out, 24000)
        stop.set()
        if ws is not None:
            try: ws.close(timeout=2)
            except Exception: pass
        if receiver is not None: receiver.join(timeout=3)
        report['finished_s'] = stamp(); save()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, default=Path('artifacts/voice-cloud/prompts-01/manifest.json'))
    args = parser.parse_args()
    report = run(args.out, args.manifest)
    print(json.dumps({key: report.get(key) for key in ('complete', 'attempts', 'audio_inputs',
        'tool_result_continuations', 'error_type', 'http_status', 'max_json_event_bytes',
        'max_audio_delta_chars', 'max_audio_delta_pcm_bytes')}, ensure_ascii=False))
    return 0 if report['complete'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
