"""Describe overlapping voice stages from saved conversation_test reports.

No device access. Comparisons use only the valid, source-matched common prefix;
timelines are independent intervals, never an additive latency breakdown.
"""
import argparse
import json
import math
from pathlib import Path
import statistics


TIMING_NOTE = (
    'Input end is a board-clock estimate using host waveOut completion and the '
    'first playback event USB receipt, as in stream_latency.py. It includes USB '
    'delivery/polling delay; firmware changes to telemetry can change that error. '
    'playback means first PCM received, not audible speech. speaker_start means '
    'board DMA submission, not acoustic onset; its retrospective USB receipt '
    'is never a clock anchor.'
)
METRIC_NOTES = {
    'input_end_to_capture_end_s': 'USB-aligned estimate; includes endpoint decision and capture scheduling, not VAD silence alone.',
    'asr_handshake_s': 'ASR connect to connected: DNS/TCP/TLS/WebSocket together.',
    'asr_tail_s': 'Capture end to final transcript delivered to Agent; includes commit, remaining upload and server result.',
    'capture_end_to_upload_end_s': 'wake.end_at to vad_end. In plugins/speech/qianwen.c, agent_qwen_live emits vad_end only after the input->next/asr_feed loop reaches EOF. This interval includes capture finalization/CRC commit and remaining PCM submission to the transport; it is not an isolated Flash or CRC measurement, nor a server upload acknowledgment.',
    'capture_end_to_cue_end_s': 'wake.end_at to wake.cue_end_at; includes final flush/CRC and completion cue on a successful capture. This bounds what overlapping ASR finish with that cue could save, not a measured optimization gain.',
    'cue_end_to_upload_end_s': 'wake.cue_end_at to vad_end; later capture cleanup/scheduling and remaining upload. Do not label it pure network time.',
    'asr_upload_stats': 'Optional cumulative wall times for the entire live loop after ASR start, not just the capture tail: source_ms covers input reads; feed_ms includes sending, backpressure and incoming-event processing; wait_ms covers polling while no source PCM is ready. All include task preemption. samples/chunks count only fully successful feed calls; a failed call may have sent some bytes. complete means local EOF with no feed error, not server acknowledgment or final transcript success. Missing diagnostics stay null.',
    'asr_after_upload_s': 'vad_end to asr_text. agent_qwen_live emits vad_end immediately before asr_finish, whose finish-task/result/connection-close path completes before asr_text; this is the ASR tail after local audio submission.',
    'planning_to_final_body_s': 'LLM entry to final no-tools request body sent; includes planning/tools and request upload, not pure inference.',
    'final_body_to_first_text_s': 'Final request body sent to first final text telemetry.',
    'first_text_to_tts_submission_s': 'First final text telemetry to first TTS feed-call telemetry; not a server acknowledgment. USB mirror order can bias this marker.',
    'tts_handshake_s': 'May overlap model generation; never add it to full LLM duration.',
    'overlap': 'Intersection of wall-clock windows, not CPU time or continuous network activity. All reported overlaps and durations are nonnegative.',
}


def number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def upload_stats(events):
    if not events:
        return None, []
    if len(events) != 1:
        return None, ['Duplicate ASR upload diagnostics; not selecting one silently']
    try:
        data = json.loads(events[0].get('text', ''))
        keys = ('samples', 'chunks', 'source_ms', 'feed_ms', 'wait_ms')
        if not isinstance(data, dict) or any(type(data.get(k)) is not int or data[k] < 0 for k in keys):
            raise ValueError('counters must be nonnegative integers')
        if type(data.get('complete')) is not bool:
            raise ValueError('complete must be boolean')
        if not data['chunks'] <= data['samples'] <= data['chunks'] * 1024:
            raise ValueError('sample/chunk counts disagree')
    except (ValueError, TypeError) as error:
        return None, [f'Invalid ASR upload diagnostics: {error}']
    return {key: data[key] for key in (*keys, 'complete')}, []


def trial_row(trial, directory, run_complete):
    if not isinstance(trial, dict):
        raise ValueError(f'{directory}: trial must be an object')
    issues, missing = [], []

    def object_field(key):
        value = trial.get(key)
        if value is None:
            return {}
        if not isinstance(value, dict):
            issues.append(f'Malformed {key}: expected an object')
            return {}
        return value

    grouped = {}
    events = trial.get('events', [])
    if not isinstance(events, list):
        raise ValueError(f'{directory}: trial events must be a list')
    for event in events:
        if (not isinstance(event, dict) or not isinstance(event.get('stage'), str)
                or not number(event.get('time_ms')) or event['time_ms'] < 0):
            issues.append('Malformed event: stage and finite time_ms are required')
            continue
        grouped.setdefault(event['stage'], []).append(event)

    def event(stage, last=False):
        values = grouped.get(stage, [])
        return values[-1 if last else 0] if values else {}

    def at(stage, last=False):
        return event(stage, last).get('time_ms')

    def duration(name, start, end):
        if not number(start) or not number(end):
            missing.append(name)
            return None
        if end < start:
            issues.append(f'{name}: reversed markers ({start} -> {end}); not treated as a negative duration')
            return None
        return round((end - start) / 1000, 6)

    wake = object_field('wake')
    pcm = event('playback')
    playback = object_field('utterance_playback')
    finished = playback.get('finished')
    observed = pcm.get('observed')
    input_end = (pcm['time_ms'] - 1000 * (observed - finished)
                 if number(observed) and number(finished) and pcm else None)
    metrics = {}
    intervals = {
        'wake_to_capture_s': (wake.get('detected_at'), wake.get('record_at')),
        'input_end_to_capture_end_s': (input_end, wake.get('end_at')),
        'capture_s': (wake.get('record_at'), wake.get('end_at')),
        'capture_end_to_upload_end_s': (wake.get('end_at'), at('vad_end')),
        'capture_end_to_cue_end_s': (wake.get('end_at'), wake.get('cue_end_at')),
        'cue_end_to_upload_end_s': (wake.get('cue_end_at'), at('vad_end')),
        'asr_handshake_s': (at('asr_connect'), at('asr_connected')),
        'asr_task_start_s': (at('asr_connected'), at('asr_started')),
        'asr_ready_to_first_upload_s': (at('asr_started'), at('asr_audio')),
        'asr_tail_s': (wake.get('end_at'), at('asr_text', True)),
        'asr_after_upload_s': (at('vad_end'), at('asr_text', True)),
        'asr_to_llm_handoff_s': (at('asr_text', True), at('llm')),
        'planning_to_final_body_s': (at('llm'), at('llm_body_sent')),
        'final_body_to_first_text_s': (at('llm_body_sent'), at('llm_first_text')),
        'first_text_to_tts_submission_s': (at('llm_first_text'), at('tts_text')),
        'llm_s': (at('llm'), at('llm_done')),
        'tts_handshake_s': (at('tts_connect'), at('tts_connected')),
        'tts_task_start_s': (at('tts_connected'), at('tts_started')),
        'tts_submission_to_pcm_s': (at('tts_text'), at('playback')),
        'speaker_prefill_s': (at('playback'), at('speaker_start')),
        'input_end_to_pcm_s': (input_end, at('playback')),
        'input_end_to_speaker_s': (input_end, at('speaker_start')),
    }
    for name, (start, end) in intervals.items():
        metrics[name] = duration(name, start, end)

    def overlap(a, b, c, d):
        if not all(number(x) for x in (a, b, c, d)) or b < a or d < c:
            return None
        return round(max(0, min(b, d) - max(a, c)) / 1000, 6)

    llm_done, pcm_at = at('llm_done'), at('playback')
    known = number(llm_done) and number(pcm_at)
    overlaps = {
        'asr_upload_window_with_capture_s': overlap(at('asr_audio'), at('asr_done'), wake.get('record_at'), wake.get('end_at')),
        'tts_handshake_with_llm_s': overlap(at('tts_connect'), at('tts_connected'), at('llm'), llm_done),
        'tts_task_with_llm_s': overlap(at('tts_connect'), at('tts_done'), at('llm'), llm_done),
        'pcm_before_llm_done': pcm_at < llm_done if known else None,
        'pcm_lead_before_llm_done_s': round(max(0, llm_done - pcm_at) / 1000, 6) if known else None,
        'pcm_after_llm_done_s': round(max(0, pcm_at - llm_done) / 1000, 6) if known else None,
    }
    # Signed event ordering is diagnostic data, never a negative duration. It
    # also exposes older firmware's first-text marker after TTS publication.
    first_text, tts_text = at('llm_first_text'), at('tts_text')
    marker_offsets = {
        'tts_submission_relative_first_text_s': round((tts_text-first_text)/1000, 6)
        if number(first_text) and number(tts_text) else None,
        'tts_connect_relative_llm_done_s': round((at('tts_connect')-llm_done)/1000, 6)
        if number(at('tts_connect')) and number(llm_done) else None,
    }
    if not run_complete:
        issues.append('Run is incomplete; retained for diagnosis, excluded from paired statistics')
    if trial.get('complete') is not True:
        issues.append('Trial is incomplete')
    reported_error = trial.get('error')
    voice_error = object_field('voice').get('error')
    if reported_error or voice_error not in (None, 'ok') or grouped.get('error'):
        issues.append(f'Trial reports failure: {reported_error or voice_error or "error event"}')
    if metrics['input_end_to_pcm_s'] is None:
        issues.append('No valid input-end to first-PCM estimate')
    wake_source, utterance_source = object_field('wake_source'), object_field('utterance_source')
    identity = {
        'id': trial.get('id'), 'wake_language': trial.get('wake_language'),
        'wake_sha256': wake_source.get('source_sha256'),
        'utterance_sha256': utterance_source.get('source_sha256'),
    }
    if not all(isinstance(x, str) and x for x in identity.values()):
        issues.append('Missing trial identity/source hash')
    for key in ('wake_sha256', 'utterance_sha256'):
        value = identity[key]
        if not isinstance(value, str) or len(value) != 64 or any(c not in '0123456789abcdefABCDEF' for c in value):
            issues.append(f'Invalid {key}')
    conditions = {'wake_source': wake_source.get('gain'), 'utterance_source': utterance_source.get('gain'),
                  'output_device': playback.get('device_name')}
    firmware = object_field('status').get('version')
    upload, diagnostic_issues = upload_stats(grouped.get('asr_upload_stats', []))
    return {
        'directory': str(directory), 'identity': identity, 'conditions': conditions,
        'firmware': firmware,
        'complete': run_complete and trial.get('complete') is True,
        'eligible': not issues, 'issues': issues, 'missing_metrics': missing,
        'input_end_estimated_board_ms': round(input_end, 3) if input_end is not None else None,
        'metrics_s': metrics, 'overlap': overlaps, 'signed_marker_offsets_s': marker_offsets,
        'asr_upload_stats': upload, 'diagnostic_issues': diagnostic_issues,
        'asr_text': event('asr_text', True).get('text'),
        'tool_done_events': len(grouped.get('tool_done', [])),
        'empty_asr_final_events': sum(not e.get('text') for e in grouped.get('asr_final', [])),
    }


def load(paths):
    rows = []
    for path in paths:
        path = Path(path)
        source = path if path.is_file() else path / 'report.json'
        report = json.loads(source.read_text(encoding='utf8'))
        if not isinstance(report, dict) or not isinstance(report.get('trials'), list):
            raise ValueError(f'{source}: expected a conversation report with a trials list')
        rows.extend(trial_row(t, source.parent, report.get('complete') is True) for t in report['trials'])
    return rows


def distribution(values):
    return {'n': len(values), 'median': statistics.median(values), 'mean': statistics.mean(values),
            'min': min(values), 'max': max(values)} if values else {'n': 0}


def compare(before, after, strict=False):
    warnings, prefix = [], 0
    for left, right in zip(before, after):
        if left['identity'] != right['identity'] or not all(left['identity'].values()):
            warnings.append(f'Source/order mismatch at index {prefix}; later samples are not compared')
            break
        prefix += 1
    if len(before) != len(after):
        warnings.append(f'Unequal sample counts: baseline={len(before)}, candidate={len(after)}; distributions use the common prefix only')
    usable = 0
    for left, right in zip(before[:prefix], after[:prefix]):
        if not left['eligible'] or not right['eligible']:
            warnings.append(f'Incomplete, failed or invalid sample at prefix index {usable}; paired statistics stop here')
            break
        if left['conditions'] != right['conditions']:
            warnings.append(f'Playback gain/device differs at prefix index {usable}; paired statistics stop here')
            break
        usable += 1
    if not usable:
        warnings.append('No valid matched prefix available for paired statistics')
    if strict and (warnings or prefix != len(before) or prefix != len(after)):
        raise ValueError('; '.join(warnings))
    statistics_by_metric = {}
    if usable:
        for key in before[0]['metrics_s']:
            pairs = [(a['metrics_s'][key], b['metrics_s'][key]) for a, b in zip(before[:usable], after[:usable])
                     if a['metrics_s'][key] is not None and b['metrics_s'][key] is not None]
            statistics_by_metric[key] = {
                'baseline': distribution([a for a, _ in pairs]),
                'candidate': distribution([b for _, b in pairs]),
                'candidate_minus_baseline_s': distribution([round(b - a, 6) for a, b in pairs]),
            }
    return {
        'schema': 'agent.voice.flow_latency/1', 'baseline_count': len(before), 'candidate_count': len(after),
        'matched_source_prefix': prefix, 'compared_pairs': usable,
        'excluded_baseline': len(before) - usable, 'excluded_candidate': len(after) - usable,
        'warnings': warnings, 'paired_metrics': statistics_by_metric,
        'baseline': before, 'candidate': after,
        'timing_note': TIMING_NOTE, 'metric_notes': METRIC_NOTES,
        'analysis_note': 'Diagnostic measurements only; no improvement or causal claim. Synthetic sources, cloud load and accumulated context are not randomized. Independent intervals overlap and must not be summed or converted into a shared percentage breakdown. Missing telemetry is null, never inferred from neighboring stages.',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, nargs='+', required=True)
    parser.add_argument('--candidate', type=Path, nargs='+', required=True)
    parser.add_argument('--strict', action='store_true', help='Reject unequal counts, mismatches, incomplete/error trials or changed playback conditions')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        report = compare(load(args.baseline), load(args.candidate), args.strict)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    print(json.dumps({key: report[key] for key in ('baseline_count', 'candidate_count', 'matched_source_prefix',
                                                  'compared_pairs', 'warnings', 'paired_metrics')}, ensure_ascii=False))


if __name__ == '__main__':
    main()
