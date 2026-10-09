"""Separate overlapping capture/ASR work from the delay after an acoustic prompt.

Uses board timestamps within a turn. Host waveOut completion is aligned with
the immediate first-PCM event's USB receipt; this includes USB polling delay.
Retrospective speaker_start telemetry is never used as a host clock anchor.
"""
import argparse
import json
from pathlib import Path
import statistics


def load(paths, streaming):
    rows = []
    for path in paths:
        report = json.loads((path / 'report.json').read_text(encoding='utf8'))
        assert report['complete'], f'Incomplete run: {path}'
        for trial in report['trials']:
            assert trial['complete'], f'Incomplete trial: {path}/{trial["id"]}'
            events = {e['stage']: e for e in trial['events']}
            pcm = events['playback']
            input_end = pcm['time_ms'] - 1000 * (pcm['observed'] - trial['utterance_playback']['finished'])
            row = {
                'directory': str(path), 'id': trial['id'], 'wake_language': trial['wake_language'],
                'utterance_sha256': trial['utterance_source']['source_sha256'],
                'wake_sha256': trial['wake_source']['source_sha256'],
                'input_end_to_pcm_s': (pcm['time_ms'] - input_end) / 1000,
                'min_heap': trial['status']['min_heap'], 'free_heap': trial['status']['free_heap'],
                'worker_stack': trial['status']['worker_stack'],
                'vad_stack': trial['wake']['verify']['stack'],
                'underruns': trial['voice']['underruns'], 'dma_lost': trial['wake']['dma_lost'],
                'asr_text': events['asr_text'].get('text'),
                'llm_text': ''.join(x for x in trial['lines'] if x and not x.startswith(('@', '{', 'I (', 'W (', 'E ('))),
                'tool_calls': sum(e['stage'] == 'tool_done' for e in trial['events']),
                'light': trial['light'],
            }
            if streaming:
                at = lambda stage: events[stage]['time_ms']
                capture_end = trial['wake']['end_at']
                row['phases_s'] = {
                    'endpoint_and_capture_commit': (capture_end - input_end) / 1000,
                    'remaining_asr_upload_and_final': (at('asr_text') - capture_end) / 1000,
                    'agent_handoff': (at('llm') - at('asr_text')) / 1000,
                    'deepseek_and_tools': (at('llm_done') - at('llm')) / 1000,
                    'before_tts': (at('tts_connect') - at('llm_done')) / 1000,
                    'tts_connect': (at('tts_connected') - at('tts_connect')) / 1000,
                    'tts_task_start': (at('tts_started') - at('tts_connected')) / 1000,
                    'tts_text_to_pcm': (at('playback') - at('tts_started')) / 1000,
                    'speaker_prefill': (at('speaker_start') - at('playback')) / 1000,
                }
                row['input_end_to_speaker_s'] = (at('speaker_start') - input_end) / 1000
                row['capture_s'] = (capture_end - trial['wake']['record_at']) / 1000
                row['wake_to_capture_s'] = (trial['wake']['record_at'] - trial['wake']['detected_at']) / 1000
                row['asr_connect_s'] = (at('asr_connected') - at('asr_connect')) / 1000
                row['asr_sent_before_capture_end_s'] = (capture_end - at('asr_audio')) / 1000
                row['asr_first_partial_before_capture_end_s'] = (
                    (capture_end - at('asr_partial')) / 1000 if 'asr_partial' in events else None)
                row['tts_first_pcm_before_task_done_s'] = (at('tts_done') - at('playback')) / 1000
                row['capture_limited'] = trial['wake']['reason'] == 4
                assert abs(sum(row['phases_s'].values()) - row['input_end_to_speaker_s']) < .0001
            else:
                at = lambda stage: events[stage]['time_ms']
                row['phases_s'] = {
                    'endpoint_to_file_asr': (at('upload') - input_end) / 1000,
                    'file_asr': (at('asr_text') - at('upload')) / 1000,
                    'deepseek_and_tools': (at('tts') - at('llm')) / 1000,
                    'tts_to_pcm': (at('playback') - at('tts')) / 1000,
                }
            rows.append(row)
    return rows


def distribution(values):
    return {'median': statistics.median(values), 'mean': statistics.mean(values),
            'min': min(values), 'max': max(values)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline', type=Path, nargs='+', required=True)
    p.add_argument('--candidate', type=Path, nargs='+', required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    before, after = load(a.baseline, False), load(a.candidate, True)
    identity = lambda row: tuple(row[k] for k in ('id', 'wake_language', 'utterance_sha256', 'wake_sha256'))
    assert [identity(r) for r in before] == [identity(r) for r in after], 'Source/order mismatch'
    means = {key: statistics.mean(r['phases_s'][key] for r in after) for key in after[0]['phases_s']}
    total = sum(means.values())
    report = {
        'matched_pairs': len(after), 'before': before, 'after': after,
        'input_end_to_pcm_s': {name: distribution([r['input_end_to_pcm_s'] for r in rows])
                               for name, rows in [('before', before), ('after', after)]},
        'input_end_to_speaker_s': distribution([r['input_end_to_speaker_s'] for r in after]),
        'wake_to_capture_s': distribution([r['wake_to_capture_s'] for r in after]),
        'mean_phase_structure': {key: {'seconds': seconds, 'percent': 100 * seconds / total}
                                 for key, seconds in means.items()},
        'timing_note': 'Host waveOut end to first PCM includes USB receipt/polling delay. Speaker start is board DMA submission, not measured acoustic speech onset.',
        'experiment_note': 'Matched synthetic sources; different test times/cloud queues and accumulated context. Not randomized causal evidence or a human recognition benchmark.',
    }
    pair = report['input_end_to_pcm_s']
    report['median_reduction_percent'] = 100 * (1 - pair['after']['median'] / pair['before']['median'])
    a.out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    print(json.dumps({key: report[key] for key in ['matched_pairs', 'input_end_to_pcm_s',
                      'input_end_to_speaker_s', 'median_reduction_percent', 'mean_phase_structure']}, ensure_ascii=False))


if __name__ == '__main__':
    main()
