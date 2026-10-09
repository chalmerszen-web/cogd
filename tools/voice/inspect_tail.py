"""Audit saved continuous_rewake tail markers; no device or provider access.

Intervals overlap and are not an additive breakdown. Capture clocks require a
per-turn diagnostic tuple, or the matching last post-group wake snapshot.
"""
import argparse
import hashlib
import json
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture_tail(events, current, samples):
    """Read diagnostic v1/v2 tuples; malformed trace never becomes a clock."""
    traces = [e for e in events if e.get('stage') in ('capture_tail_v1','capture_tail_v2')]
    if not traces:
        return None
    if len(traces) != 1:
        raise ValueError('Duplicate capture tail trace')
    try:
        data = json.loads(traces[0]['text'])
    except (ValueError, KeyError, TypeError) as error:
        raise ValueError('Invalid capture_tail_v1 JSON') from error
    names = ('record_at','stopped_at','end_at','eof_at','commit_begin_at',
             'commit_end_at','cue_begin_at','cue_end_at','cleanup_at','samples','error')
    if traces[0]['stage'] == 'capture_tail_v2':
        names += ('preheat_at','cue_enqueue_at','zero_samples','rate')
    if (not isinstance(data,list) or len(data) != len(names) or
        any(type(x) is not int or not 0 <= x <= 0xffffffff for x in data)):
        raise ValueError('Invalid capture_tail_v1 tuple')
    clock = dict(zip(names,data))
    if (not clock['record_at'] or current.get('record_at') != clock['record_at'] or
        type(samples) is not int or samples <= 0 or samples != clock['samples'] or clock['error']):
        raise ValueError('capture_tail_v1 identity/samples/error disagree')
    offsets = [(x-clock['record_at']) & 0xffffffff for x in data[:9]]
    if any(x == 0 for x in data[:9]) or offsets != sorted(offsets) or offsets[-1] > 35000:
        raise ValueError('capture_tail_v1 clocks incomplete/reversed/out of turn')
    if 'preheat_at' in clock:
        started = (clock['preheat_at']-clock['record_at']) & 0xffffffff
        queued = (clock['cue_enqueue_at']-clock['record_at']) & 0xffffffff
        elapsed = (clock['cue_enqueue_at']-clock['preheat_at']) & 0xffffffff
        if (clock['rate'] not in (16000,24000) or not clock['preheat_at'] or not clock['cue_enqueue_at'] or
            not offsets[3] <= started <= offsets[4] or not offsets[6] <= queued <= offsets[7] or
            clock['zero_samples'] > 180*clock['rate']//1000 or
            elapsed*clock['rate']+1000*clock['zero_samples'] < 180*clock['rate']):
            raise ValueError('capture_tail_v2 preheat/zero lead incomplete or out of order')
    return clock


def audit(report, acoustic):
    rows = []
    source_trials = report['trials']
    acoustic_trials = acoustic.get('trials', [])
    # A rejected common acoustic fit intentionally produces no per-turn rows.
    # Preserve that rejection while still inspecting independent board clocks.
    # No source/answer onset is inferred or promoted by this diagnostic audit.
    if (not acoustic_trials and acoustic.get('status') == 'unknown' and
        acoustic.get('source_alignment', {}).get('accepted') is False):
        acoustic_trials = [dict(round=t['round'],attempt=a['attempt'],
            source_match_accepted=False,status='unknown')
            for t in source_trials for a in t.get('attempts', []) if a.get('prompt_playback')]
    if len(source_trials) != len(acoustic_trials):
        raise ValueError('Acoustic/source trial counts differ')
    for index, (trial, sound) in enumerate(zip(source_trials, acoustic_trials)):
        if trial['round'] != sound['round']:
            raise ValueError('Acoustic/source round order differs')
        attempts = [a for a in trial.get('attempts', []) if a.get('prompt_playback')]
        if len(attempts) != 1 or attempts[0]['attempt'] != sound['attempt']:
            raise ValueError('One unique matching played prompt per round required')
        attempt = attempts[0]
        issues = []

        def marker(name):
            events = [e for e in attempt['events'] if e.get('stage') == name]
            if len(events) != 1:
                issues.append(f'{name}: missing or duplicate marker')
                return None
            value = events[0].get('time_ms')
            if type(value) is not int or value < 0:
                issues.append(f'{name}: invalid board time')
                return None
            return value

        clocks = {name: marker(name) for name in ('vad_end', 'asr_finish',
            'asr_final_received', 'asr_text', 'capture_radio_restored',
            'candidate_miss', 'fast_local_intent', 'tool_done', 'fast_local_reply',
            'playback', 'speaker_start')}

        def interval(a, b):
            if a is None or b is None:
                return None
            if b < a:
                issues.append(f'Reversed interval {a} -> {b}')
                return None
            return b-a

        intervals = {
            'upload_eof_to_final_asr_ms': interval(clocks['vad_end'], clocks['asr_text']),
            'final_event_to_asr_text_ms': interval(clocks['asr_final_received'], clocks['asr_text']),
            'final_asr_to_capture_radio_restored_ms': interval(clocks['asr_text'], clocks['capture_radio_restored']),
            'radio_restore_to_local_effect_ms': interval(clocks['capture_radio_restored'], clocks['tool_done']),
            'final_asr_to_first_pcm_ms': interval(clocks['asr_text'], clocks['playback']),
            'first_pcm_to_dma_ms': interval(clocks['playback'], clocks['speaker_start']),
            'capture_end_to_upload_eof_ms': None,
            'capture_end_to_cleanup_ms': None,
            'upload_eof_to_cleanup_ms': None,
            'physical_stop_to_confirmation_ms': None,
            'confirmation_to_eof_publication_ms': None,
            'commit_wall_ms': None,
            'confirmation_to_cue_begin_ms': None,
            'cue_wall_ms': None,
            'cue_end_to_cleanup_ms': None,
        }
        capture = None
        current = attempt.get('last_state', {}).get('wake', {})
        upload = [e for e in attempt['events'] if e.get('stage') == 'asr_upload_stats']
        try:
            samples = json.loads(upload[0]['text'])['samples'] if len(upload) == 1 else None
        except (ValueError, KeyError, TypeError):
            samples = None
        has_trace = any(e.get('stage') in ('capture_tail_v1','capture_tail_v2') for e in attempt['events'])
        try:
            capture = capture_tail(attempt['events'],current,samples)
        except ValueError as error:
            issues.append(str(error))
        if capture:
            pairs = {'physical_stop_to_confirmation_ms':('stopped_at','end_at'),
                'confirmation_to_eof_publication_ms':('end_at','eof_at'),
                'commit_wall_ms':('commit_begin_at','commit_end_at'),
                'confirmation_to_cue_begin_ms':('end_at','cue_begin_at'),
                'cue_wall_ms':('cue_begin_at','cue_end_at'),
                'cue_end_to_cleanup_ms':('cue_end_at','cleanup_at')}
            for name,(a,b) in pairs.items():intervals[name] = (capture[b]-capture[a]) & 0xffffffff
            intervals['capture_end_to_upload_eof_ms'] = interval(capture['end_at'],clocks['vad_end'])
            intervals['capture_end_to_cleanup_ms'] = (capture['cleanup_at']-capture['end_at']) & 0xffffffff
            intervals['upload_eof_to_cleanup_ms'] = interval(clocks['vad_end'],capture['cleanup_at'])
        elif not has_trace and index == len(source_trials)-1:
            final = report.get('wake_after', {})
            identity_keys = ('detected_at', 'record_at', 'wakes')
            identity = (all(type(current.get(k)) is int and current[k] > 0 and
                            current[k] == final.get(k) for k in identity_keys) and
                        type(samples) is int and samples > 0 and samples == final.get('samples') and
                        type(final.get('end_at')) is int and final['end_at'] >= final['record_at'] and
                        type(final.get('cue_end_at')) is int and final['cue_end_at'] >= final['end_at'])
            if identity:
                capture = {k: final[k] for k in (*identity_keys, 'samples', 'end_at', 'cue_end_at')}
                intervals['capture_end_to_upload_eof_ms'] = interval(final['end_at'], clocks['vad_end'])
                intervals['capture_end_to_cleanup_ms'] = interval(final['end_at'], final['cue_end_at'])
                intervals['upload_eof_to_cleanup_ms'] = interval(clocks['vad_end'], final['cue_end_at'])
            else:
                issues.append('Final capture snapshot does not match last played turn; clocks omitted')
        cue = sound.get('endpoint_cue_acoustic', {})
        acoustic_metrics = {'source_end_to_cue_start_s': None,
            'cue_end_to_answer_candidate_s': None, 'source_end_to_answer_candidate_s': None}
        if sound.get('source_match_accepted') and cue.get('present'):
            acoustic_metrics['source_end_to_cue_start_s'] = cue['start_s']-sound['input_active_end_recording_s']
            if sound.get('status') == 'candidate':
                acoustic_metrics['cue_end_to_answer_candidate_s'] = sound['acoustic_answer_onset_candidate_s']-cue['end_s']
                acoustic_metrics['source_end_to_answer_candidate_s'] = sound['acoustic_answer_latency_candidate_s']
        rows.append(dict(round=trial['round'], terminal=attempt.get('terminal'),
            strict_input=trial.get('input_complete'), sound_status=sound.get('status'),
            clocks=clocks, capture_snapshot=capture, intervals_ms=intervals,
            acoustic_metrics_s=acoustic_metrics, issues=issues))
    return dict(schema='agent.voice.tail_audit/1', rows=rows,
        acoustic_analysis_status=acoustic.get('status'),
        acoustic_analysis_reason=acoustic.get('reason'),
        notes=['Earlier rounds require a valid identity-matched post-join capture_tail_v1; absent traces never fabricate clocks.',
            'capture end includes acquisition/confirmation join; cue_end_at is capture cleanup, not exact acoustic cue end.',
            'vad_end is local upload EOF, not acoustic user end or provider acknowledgment.',
            'Intervals overlap; never sum them or claim CPU/network attribution.',
            'Retrospective DMA receipt is not a host clock anchor.',
            'v2 minimum settling counts elapsed silent output plus queued zeros; not a measured acoustic onset.',
            'Acoustic candidates use fixed sweep exclusion; ASR is not phoneme truth.',
            'A single three-turn group cannot prove statistical speed gains.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--analysis', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if Path(args.analysis).name != args.analysis:
        parser.error('--analysis must be a filename')
    report_path = args.directory/'report.json'
    analysis_path = args.directory/args.analysis
    report = json.loads(report_path.read_text(encoding='utf8'))
    sound = json.loads(analysis_path.read_text(encoding='utf8'))
    if sound.get('report_sha256') != sha(report_path):
        raise ValueError('Acoustic analysis does not refer to this report')
    if sound.get('recording_sha256') != sha(args.directory/'speaker.wav'):
        raise ValueError('Acoustic analysis does not refer to this recording')
    result = audit(report,sound)
    result['input_hashes'] = {str(p):sha(p) for p in (report_path,analysis_path)}
    result['script_sha256'] = sha(Path(__file__))
    with args.out.open('x',encoding='utf8') as f:
        f.write(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    for row in result['rows']:
        print(json.dumps(dict(round=row['round'], intervals=row['intervals_ms'],
            acoustic=row['acoustic_metrics_s']),ensure_ascii=False))


if __name__ == '__main__':
    main()
