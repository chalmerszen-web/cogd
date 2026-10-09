"""Bounded same-connection prefetch protocol experiment, using existing synthetic WAVs.

Three independent host file sessions, not board/acoustic acceptance. Audio is
only cached; no microphone, speaker or device tool is used. Run once per output
directory; this is not a production policy for deciding semantic equivalence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import wave

from input_integrity import complete_input


def analyze(report, events, expected, required, rejected):
    """Revisions identify stale replies; exact fixture ASR gates this experiment."""
    revision = 0
    responses = {}
    items = {}
    pending_item = None
    for row in events:
        event = row['event'];kind = event.get('type');at = row['observed_s']
        if kind == 'input_audio_buffer.speech_started':
            item = event.get('item_id')
            if not item or item in items:
                raise ValueError('Missing or reused input identity')
            revision += 1
            items[item] = dict(revision=revision)
            for response in responses.values():
                response.setdefault('invalidated_s', at)
        elif kind == 'input_audio_buffer.committed':
            item = event.get('item_id')
            if item not in items or items[item].get('committed') or pending_item is not None:
                raise ValueError('Missing, duplicated or ambiguous input commit')
            items[item]['committed'] = True
            pending_item = item
        elif kind == 'conversation.item.input_audio_transcription.completed':
            item = event.get('item_id');text = event.get('transcript')
            if item not in items or not isinstance(text, str) or (
                    'text' in items[item] and items[item]['text'] != text):
                raise ValueError('Unknown input or changed final ASR')
            items[item]['text'] = text
        elif kind == 'response.created':
            ident = event.get('response', {}).get('id')
            if not ident or ident in responses or pending_item is None:
                raise ValueError('Missing/reused response identity or ambiguous source')
            source_revision = items[pending_item]['revision']
            responses[ident] = dict(revision=source_revision, input_item_id=pending_item,
                                    created_s=at, pcm_bytes=0, text='')
            if source_revision != revision:
                responses[ident]['invalidated_s'] = at
            pending_item = None
        elif kind.startswith('response.'):
            ident = event.get('response_id') or event.get('response', {}).get('id')
            if not ident:
                continue
            if ident not in responses:
                raise ValueError('Output before response identity')
            response = responses[ident]
            if kind == 'response.audio.delta':
                response.setdefault('first_pcm_s', at)
                response['pcm_bytes'] += row['audio_pcm_bytes']
            elif kind in ('response.audio_transcript.delta', 'response.text.delta'):
                response['text'] += event.get('delta', '')
            elif kind == 'response.done':
                response['status'] = event['response'].get('status')
                response['done_s'] = at
    transcript = ''.join(item.get('text', '') for item in items.values())
    intact = bool(items) and all(item.get('committed') and 'text' in item for item in items.values()) and complete_input(expected, transcript)
    latest = [r for r in responses.values() if r['revision'] == revision and 'invalidated_s' not in r]
    last = latest[-1] if latest else None
    content = bool(last and required in last['text'] and not any(w in last['text'] for w in rejected))
    # File end is only a host input reference, not an acoustic endpoint. A
    # counterfactual 700ms hold permits comparing preparation, not playback.
    gate = report.get('input_active_end_estimate_s', float('inf'))+.7
    return dict(complete_input=intact, transcript=transcript, revisions=revision,
                responses=responses, expected_content=content,
                latest_candidate_valid=bool(report['complete'] and intact and content and last and
                    last.get('status') == 'completed' and last['pcm_bytes']),
                latest_pcm_before_file_end=bool(last and last.get('first_pcm_s', float('inf')) < gate-.7),
                latest_pcm_before_700ms_hold=bool(last and last.get('first_pcm_s', float('inf')) < gate),
                latest_pcm_after_file_end_s=last.get('first_pcm_s', gate)-(gate-.7) if last else None,
                discarded_response_ids=[rid for rid,r in responses.items() if 'invalidated_s' in r],
                playback_authorized=False, device_tools_executed=False,
                limitation='Host protocol/file timing only; exact synthetic fixture ASR/content check, not a general semantic validator or device latency.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    if args.execute:
        from omni_endpoint_probe import run
    args.out.mkdir(parents=True, exist_ok=False)
    manifest = Path('artifacts/voice-cloud/prompts-01/manifest.json')
    prompts = {p['id']:p for p in json.loads(manifest.read_text(encoding='utf8'))['prompts']}
    cases = [('greeting', ['greeting'], '小言', []),
             ('blue', ['blue'], '蓝', []),
             ('blue_then_green', ['blue', 'green'], '绿', ['蓝'])]
    config = dict(modalities=['text','audio'], voice='Tina', input_audio_format='pcm',
        output_audio_format='pcm', enable_search=False, tools=[],
        turn_detection=dict(type='server_vad', threshold=.1, silence_duration_ms=200),
        instructions='你是小言。用一句简短中文回答，最多16字。这是可丢弃的预响应测试，没有连接真实硬件，不得声称已执行操作。'
        '灯色指令只说准备设成什么颜色；如果后续改口，以最新颜色为准。')
    config_path = args.out/'session.json'
    config_path.write_text(json.dumps(config, ensure_ascii=False, indent=2), encoding='utf8')
    plan = dict(scope='host_only_no_playback_no_actions', execute=args.execute,
                model='qwen3.5-omni-flash-realtime', sessions_limit=3, attempts=[],
                source_manifest_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest())
    def save():
        (args.out/'report.json').write_text(json.dumps(plan, ensure_ascii=False, indent=2), encoding='utf8')
    save()
    for ident, ids, required, rejected in cases:
        parts = [];sources = []
        for key in ids:
            path = Path(prompts[key]['path'])
            pcm = subprocess.run([shutil.which('ffmpeg') or 'ffmpeg', '-v','error','-i',str(path),
                '-ac','1','-ar','16000','-f','s16le','-'], check=True, capture_output=True, timeout=15).stdout
            if not pcm or len(pcm)%2 or len(pcm)>160000:
                raise ValueError('Source exceeds five-second fixture bound')
            parts.append(pcm);sources.append(dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        source = args.out/(ident+'.wav')
        pcm = bytes(19200).join(parts)  # 600ms in-utterance pause; no source normalization.
        if len(pcm)>256000:
            raise ValueError('Combined fixture exceeds eight seconds')
        with wave.open(str(source), 'wb') as wav:
            wav.setparams((1,2,16000,0,'NONE','not compressed'))
            wav.writeframes(pcm)
        entry = dict(id=ident,sources=sources,input_samples=len(pcm)//2,
                     expected=''.join(prompts[i]['text'] for i in ids))
        plan['attempts'].append(entry);save()
        if args.execute:
            folder = args.out/ident
            result = run(folder,source,config_path=config_path,vad_mode='server_vad',continue_input=True)
            events = [json.loads(l) for l in (folder/'events.jsonl').read_text(encoding='utf8').splitlines()]
            entry['analysis'] = analyze(result,events,entry['expected'],required,rejected)
            save()
            print(json.dumps(dict(id=ident,complete=result['complete'],analysis=entry['analysis']),ensure_ascii=False),flush=True)
    plan['complete'] = len(plan['attempts']) == 3
    save()


if __name__ == '__main__':
    main()
