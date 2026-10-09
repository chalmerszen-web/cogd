"""One bounded host-only endpoint probe of an existing WAV.

No new recording, playback, hardware actions or retries. Optional
firmware tools are advertised for protocol inspection but never executed.
Input is converted to 16 kHz only if necessary. Defaults to real-time pace;
an explicit bounded speed permits investigating buffered-upload timing.
Manual mode commits once after all retained input; it cannot measure live VAD.
continue_input keeps uploading through provisional provider endpoints and
accepts cancelled drafts. It never plays or executes any speculative output.
"""
import argparse
from array import array
import base64
import hashlib
import json
from pathlib import Path
import queue
import re
import shutil
import subprocess
import threading
import time
import wave

import websocket
from qianwen_credentials import qianwen_key
from omni_realtime_probe import MODEL, URL, save_wav


def run(out, source=None, device_config=False, config_path=None, speed=1.0,
        chunk_samples=320, vad_mode='semantic_vad', continue_input=False):
    if not .5 <= speed <= 2 or not 160 <= chunk_samples <= 1600:
        raise ValueError('Input pacing outside diagnostic bounds')
    if vad_mode not in ('semantic_vad', 'server_vad', 'manual'):
        raise ValueError('Unknown VAD mode')
    out.mkdir(parents=True, exist_ok=False)
    reference = None
    if source is None:
        source = Path('artifacts/voice-fast/warm-noise-clip/device-clip.wav')
        reference = Path('artifacts/voice-fast/warm-noise-clip/offline-analysis.json')
        prior = json.loads(reference.read_text(encoding='utf8'))
    with wave.open(str(source), 'rb') as wav:
        fmt = (wav.getframerate(), wav.getnchannels(), wav.getsampwidth())
        pcm = wav.readframes(wav.getnframes())
    if fmt != (16000, 1, 2):
        pcm = subprocess.run([shutil.which('ffmpeg') or 'ffmpeg', '-v', 'error', '-i', str(source),
            '-ac', '1', '-ar', '16000', '-f', 's16le', '-acodec', 'pcm_s16le', '-'],
            check=True, capture_output=True, timeout=15).stdout
    assert 0 < len(pcm) <= 320000
    config = dict(modalities=['text', 'audio'], voice='Tina', input_audio_format='pcm',
                  output_audio_format='pcm', enable_search=False,
                  turn_detection=None if vad_mode=='manual' else dict(type=vad_mode, threshold=.1, silence_duration_ms=800),
                  instructions='你是小言，用一句简短中文回答。这是电脑录音测试，没有连接真实硬件，不能声称已经执行设备操作。')
    if reference:
        active_end = prior['matches']['board']['offset_s'] + prior['source_active_end_relative_s']
    else:
        active_end = len(pcm)/32000
        pcm += bytes(min(64000, 320000-len(pcm)))
    if device_config:
        code = Path('platform/espidf/voice_fast.c').read_text(encoding='utf8')
        def literal(name):
            match = re.search(r'static const char '+name+r'\[\]\s*=\s*((?:"(?:[^"\\]|\\.)*"\s*)+);', code)
            if not match: raise ValueError('missing_firmware_literal')
            return ''.join(json.loads(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"', match[1]))
        config['instructions'] = literal('instructions')
        config['tools'] = json.loads(literal('tools'))
    if config_path:
        config = json.loads(config_path.read_text(encoding='utf8'))
    if continue_input and config.get('turn_detection') is None:
        raise ValueError('Continuous draft input requires provider VAD, not manual mode')
    report = dict(scope='host_file_tools_not_executed' if device_config else 'host_file_no_tools', model=MODEL, endpoint=URL,
                  source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  source_pcm_sha256=hashlib.sha256(pcm).hexdigest(), source_samples=len(pcm)//2,
                  input_speed=speed, chunk_samples=chunk_samples,
                  input_end_mode='manual_after_all_file_samples' if config.get('turn_detection') is None else 'provider_vad',
                  pacing_note='Controlled host send rate, not an exact reconstruction of device packet arrival times. No acoustic speed claim.',
                  source_active_end_relative_s=active_end, endpoint_reference=str(reference) if reference else None,
                  endpoint_note='Prior acoustic alignment if supplied; otherwise file end only, not measured speech end. No acoustic latency claim.',
                  config=config, attempts=0, retries=0, reconnections=0, complete=False,
                  continue_input=continue_input, audio_played=False, tools_executed=False,
                  sent_samples=0, events=0, max_json_event_bytes=0, max_nonaudio_event_bytes=0,
                  max_audio_delta_pcm_bytes=0, max_audio_metadata_bytes=0, responses=[],
                  audio_key_order_compatible=True)
    def save():
        (out/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    save()
    begin = time.monotonic()
    stamp = lambda: time.monotonic()-begin
    incoming = queue.Queue(maxsize=256)
    stop = threading.Event()
    ws = None; worker = None; audio = {}; key = None
    def receive():
        try:
            while not stop.is_set():
                raw = ws.recv()
                at = stamp()
                if not raw:
                    incoming.put((at, None), timeout=1); return
                incoming.put((at, raw), timeout=1)
        except Exception:
            if not stop.is_set():
                try: incoming.put((stamp(), None), timeout=1)
                except queue.Full: pass
    try:
        key = qianwen_key()
        report['attempts'] = 1; report['connect_start_s'] = stamp(); save()
        ws = websocket.create_connection(URL, header=['Authorization: Bearer '+key],
                                         timeout=25, redirect_limit=0, enable_multithread=True)
        report['connected_s'] = stamp()
        worker = threading.Thread(target=receive, daemon=True); worker.start()
        deadline = time.monotonic()+25
        input_start = None; offset = 0; endpoint = False; response_done = False
        active_response = None
        while time.monotonic() < deadline:
            try:
                at, raw = incoming.get(timeout=.005)
                if raw is None: raise RuntimeError('unexpected_close_or_receive_failure')
                size = len(raw.encode('utf8')) if isinstance(raw, str) else len(raw)
                if size > 2*1024*1024: raise ValueError('oversize_event')
                event = json.loads(raw)
                event = json.loads(json.dumps(event, ensure_ascii=False).replace(key, '[REDACTED]'))
                kind = event.get('type')
                record = dict(observed_s=at, json_event_bytes=size, json_key_order=list(event), event=event)
                report['events'] += 1
                report['max_json_event_bytes'] = max(report['max_json_event_bytes'], size)
                if kind == 'response.audio.delta':
                    data = base64.b64decode(event['delta'], validate=True)
                    if not data or len(data) % 2: raise ValueError('invalid_pcm_delta')
                    rid = event['response_id']; output = audio.setdefault(rid, bytearray())
                    if len(output)+len(data) > 48000*30: raise ValueError('audio_limit')
                    values = array('h'); values.frombytes(data)
                    signal = next((i for i, value in enumerate(values) if abs(value) >= 64), None)
                    if signal is not None and 'first_signal_pcm_s' not in report:
                        report['first_signal_pcm_s'] = at
                        report['first_signal_pcm_sample'] = len(output)//2+signal
                    output.extend(data); report.setdefault('first_pcm_s', at)
                    order = list(event)
                    compatible = all(field in order and order.index(field) < order.index('delta')
                                     for field in ('type', 'response_id'))
                    report['audio_key_order_compatible'] &= compatible
                    record['type_and_response_id_before_delta'] = compatible
                    record['audio_pcm_bytes'] = len(data)
                    record['audio_delta_chars'] = len(event['delta'])
                    record['event'] = {k:v for k,v in event.items() if k != 'delta'}
                    meta = len(json.dumps(dict(event, delta=''), ensure_ascii=False, separators=(',', ':')).encode('utf8'))
                    report['max_audio_metadata_bytes'] = max(report['max_audio_metadata_bytes'], meta)
                    report['max_audio_delta_pcm_bytes'] = max(report['max_audio_delta_pcm_bytes'], len(data))
                else:
                    report['max_nonaudio_event_bytes'] = max(report['max_nonaudio_event_bytes'], size)
                with (out/'events.jsonl').open('a', encoding='utf8') as log:
                    log.write(json.dumps(record, ensure_ascii=False)+'\n')
                if device_config:
                    # Bounded local fixture for the production parser; includes
                    # PCM deltas, but never uploads recordings or runs tools.
                    with (out/'wire.jsonl').open('a', encoding='utf8') as log:
                        text = raw.decode('utf8') if isinstance(raw, bytes) else raw
                        log.write(text.replace(key, '[REDACTED]')+'\n')
                if kind == 'error':
                    report['server_error'] = event; raise RuntimeError('server_error')
                if kind == 'session.created':
                    ws.send(json.dumps(dict(type='session.update', session=config), ensure_ascii=False))
                    report['session_update_s'] = stamp()
                elif kind == 'session.updated':
                    report['server_session'] = event.get('session')
                    input_start = time.monotonic(); report['input_start_s'] = stamp()
                    report['input_active_end_estimate_s'] = report['input_start_s']+active_end/speed
                elif kind == 'input_audio_buffer.speech_started':
                    report.setdefault('speech_started', []).append(dict(observed_s=at, event=event))
                    if continue_input:
                        endpoint = response_done = False
                elif kind == 'response.created':
                    active_response = event.get('response', {}).get('id')
                    if continue_input:
                        response_done = False
                elif kind == 'input_audio_buffer.speech_stopped':
                    report.setdefault('speech_stopped', []).append(dict(observed_s=at, event=event))
                    if not event.get('reason'):
                        endpoint = True; report['endpoint_observed_s'] = at
                elif kind == 'conversation.item.input_audio_transcription.completed':
                    report.setdefault('transcripts', []).append(event.get('transcript'))
                elif kind in ('response.text.delta', 'response.audio_transcript.delta') and event.get('delta'):
                    report.setdefault('first_text_s', at)
                    report['reply'] = report.get('reply', '')+event['delta']
                elif kind == 'response.done':
                    report['responses'].append(dict(observed_s=at, event=event))
                    response = event.get('response', {})
                    if response.get('status') != 'completed' and not (continue_input and response.get('status') == 'cancelled'):
                        raise RuntimeError('response_not_completed')
                    if not continue_input or response.get('id') == active_response:
                        response_done = response.get('status') == 'completed'
            except queue.Empty:
                pass
            input_drained = not continue_input or (offset == len(pcm) and
                stamp() >= report.get('last_input_sent_s', 0)+.2)
            if endpoint and response_done and report.get('transcripts') and input_drained:
                report['complete'] = True; break
            if input_start is not None and (continue_input or not endpoint) and offset < len(pcm):
                chunk = pcm[offset:offset+chunk_samples*2]
                if time.monotonic() >= input_start+(offset+len(chunk))/32000/speed:
                    ws.send(json.dumps(dict(type='input_audio_buffer.append',
                                           audio=base64.b64encode(chunk).decode('ascii'))))
                    offset += len(chunk); report['sent_samples'] = offset//2
                    report['last_input_sent_s'] = stamp()
            if input_start is not None and not endpoint and offset == len(pcm) and config.get('turn_detection') is None:
                ws.send(json.dumps(dict(type='input_audio_buffer.commit')))
                report['manual_commit_s'] = stamp()
                ws.send(json.dumps(dict(type='response.create')))
                report['manual_response_create_s'] = stamp()
                endpoint = True
        if not report['complete']: report['deadline_reached'] = True
    except Exception as exc:
        report['error_type'] = type(exc).__name__
        status = getattr(exc, 'status_code', None)
        if isinstance(status, int): report['http_status'] = status
    finally:
        stop.set()
        if ws is not None:
            try: ws.close(timeout=2)
            except Exception: pass
        if worker is not None: worker.join(timeout=3)
        report['outputs'] = []
        for index, (rid, data) in enumerate(audio.items()):
            path = out/f'response-{index+1}.wav'; save_wav(path, data, 24000)
            report['outputs'].append(dict(response_id=rid, path=str(path), samples=len(data)//2,
                                          pcm_sha256=hashlib.sha256(data).hexdigest()))
        for field in ('endpoint_observed_s', 'first_text_s', 'first_pcm_s', 'first_signal_pcm_s'):
            if field in report and 'input_active_end_estimate_s' in report:
                report[field[:-2]+'_after_input_s'] = report[field]-report['input_active_end_estimate_s']
        report['finished_s'] = stamp(); save()
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--device-config', action='store_true', help='Use current firmware prompt/tools but never execute model tool calls')
    parser.add_argument('--config', type=Path, help='Explicit host-only session configuration')
    parser.add_argument('--speed', type=float, default=1.0, help='Host-only send speed, 0.5 to 2.0; not acoustic playback')
    parser.add_argument('--chunk-samples', type=int, default=320)
    parser.add_argument('--vad-mode', choices=('semantic_vad','server_vad','manual'), default='semantic_vad')
    parser.add_argument('--continue-input', action='store_true', help='Keep all input after early endpoints; retain cancelled drafts without playback/actions')
    args = parser.parse_args()
    result = run(args.out, args.source, args.device_config, args.config,
                 args.speed, args.chunk_samples, args.vad_mode, args.continue_input)
    print(json.dumps({k:result.get(k) for k in ('complete', 'attempts', 'sent_samples', 'transcripts',
        'endpoint_observed_after_input_s', 'first_pcm_after_input_s', 'error_type')}, ensure_ascii=True))
    raise SystemExit(0 if result['complete'] else 1)
