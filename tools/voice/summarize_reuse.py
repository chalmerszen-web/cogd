"""Summarize bounded connection-reuse trials without dropping failed rounds."""
import argparse
import hashlib
import json
from pathlib import Path


def read(path):
    data = path.read_bytes()
    return json.loads(data), hashlib.sha256(data).hexdigest()


def summarize(directory):
    report, digest = read(directory / 'report.json')
    analysis, analysis_hash = read(directory / 'offline-analysis.json')
    feedback, feedback_hash = read(directory / 'feedback-analysis.json')
    if any(x['report_sha256'] != digest for x in (analysis, feedback)):
        raise ValueError(f'{directory}: analysis belongs to another report')
    acoustic = {(x['round'], x['attempt']): x for x in analysis['trials']}
    cues = {x['round']: x for x in feedback['trials']}
    rows = []
    for trial in report['trials']:
        if len(trial['attempts']) != 1:
            raise ValueError(f'{directory}: expected one recorded attempt per round')
        attempt = trial['attempts'][0]
        events = trial['events']
        def first(stage):
            return next((x['time_ms'] for x in events if x['stage'] == stage), None)
        def delta(stage, origin):
            end, start = first(stage), first(origin)
            return None if end is None or start is None else end - start
        measured = acoustic.get((trial['round'], attempt['attempt']))
        if measured is None:
            # An entire group can fail the fixed alignment gates. Preserve
            # each device outcome with unknown acoustics, never drop its row.
            if analysis.get('status') != 'unknown':
                raise ValueError(f'{directory}: missing acoustic result without unknown status')
            measured = {'source_match_accepted': False, 'status': 'unknown'}
        effects = []
        for line in trial['raw_lines']:
            if line.startswith('@tool '):
                name, _, result = line[6:].partition(' ')
                effects.append({'name': name, 'result': json.loads(result)})
        rows.append({
            'round': trial['round'], 'wake_language': trial['language'],
            'first_wake_hit': attempt['triggered'], 'terminal': attempt.get('terminal'),
            'complete': trial['complete'], 'input_complete': trial['input_complete'],
            'core_task_complete': trial['core_task_complete'], 'asr': trial['asr'],
            'cold_ready_ms': delta('fast_ready', 'fast_connect'),
            'reuse_ready_ms': delta('fast_ready', 'fast_reuse'),
            'retained': first('fast_retained') is not None,
            'pcm_after_vad_ms': delta('playback', 'vad_end'),
            'speaker_after_vad_ms': delta('speaker_start', 'vad_end'),
            'receipt_speaker_after_vad_ms': delta('progress_speaker_start', 'vad_end'),
            'source_match_accepted': measured['source_match_accepted'],
            'acoustic_status': measured['status'],
            'answer_latency_candidate_s': measured.get('acoustic_answer_latency_candidate_s'),
            'answer_transcript': measured.get('offline_asr', {}).get('text'),
            'receipt_latency_candidate_s': measured.get('progress_acoustic', {}).get('energy_ack_latency_candidate_s'),
            'receipt_transcript': measured.get('progress_acoustic', {}).get('offline_asr', {}).get('text'),
            'feedback': cues[trial['round']], 'effects': effects,
            'capture_joined': [{k: e[k] for k in ('stage', 'time_ms', 'text') if k in e}
                               for e in events if e['stage'] == 'capture_joined'],
            'errors': [{k: e[k] for k in ('stage', 'time_ms', 'text') if k in e}
                       for e in events if e['stage'] in ('error', 'fast_retain_failed')],
        })
    if len(rows) != 3:
        raise ValueError(f'{directory}: expected all three continuous rounds')
    return {'directory': directory.as_posix(), 'firmware': report['before']['version'],
            'report_sha256': digest, 'analysis_sha256': analysis_hash,
            'feedback_sha256': feedback_hash, 'prefetch': report['prefetch'],
            'reuse': report['reuse'], 'rows': rows,
            'cumulative_min_heap': report['after']['min_heap'],
            'dma_lost_after': report['wake_after']['dma_lost']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directories', nargs='+', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    groups = [summarize(d) for d in args.directories]
    rows = [r for g in groups for r in g['rows']]
    result = {'accepted': False, 'groups': groups, 'rounds': len(rows),
              'complete': sum(r['complete'] and r['input_complete'] and r['core_task_complete'] for r in rows),
              'first_wakes': sum(r['first_wake_hit'] for r in rows),
              'limitations': [
                  'Acoustic times are signal/ASR candidates, not human listening judgments.',
                  'Missing alignment or playback stays unknown, including unsuccessful rounds.',
                  'Endpoint cues and receipts do not count as useful answers.',
                  'The heap minimum is cumulative since boot, not a per-group reset.',
                  'Manual-turn reuse is not proof of preparing an answer before speech ends.',
              ]}
    with args.out.open('x', encoding='utf8') as output:
        json.dump(result, output, ensure_ascii=False, indent=2)
        output.write('\n')
    print(json.dumps({k: result[k] for k in ('accepted', 'rounds', 'complete', 'first_wakes')}))


if __name__ == '__main__':
    main()
