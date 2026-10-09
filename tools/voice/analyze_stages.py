"""Review retained stage/heap diagnostics without device or network access."""
import argparse
import hashlib
import json
from pathlib import Path


def llm_requests(events):
    requests = {}
    for event in events:
        stage = event['stage']
        if 'request' not in event or not stage.startswith(('llm_plan_', 'llm_final_')):
            continue
        request = requests.setdefault(event['request'], dict(
            request=event['request'], phase=stage.split('_')[1], stages={}))
        phase = stage.split('_', 2)[2]
        if phase in request['stages']:
            raise ValueError('Duplicate LLM phase in one request')
        request['stages'][phase] = event['time_ms']
        if phase == 'measured':
            request['request_bytes'] = event['request_bytes']
            request['history_bytes'] = event['history_bytes']
    for request in requests.values():
        stamps = request['stages']
        pairs = {
            'history_select_ms': ('select_begin', 'selected'),
            'length_measure_ms': ('selected', 'measured'),
            'connection_ms': ('connect_begin', 'connected'),
            'body_send_ms': ('connected', 'sent'),
            'headers_wait_ms': ('sent', 'headers'),
            'first_byte_wait_ms': ('sent', 'first_byte'),
            'response_read_ms': ('first_byte', 'response_done'),
            'prepare_to_return_ms': ('select_begin', 'response_done'),
        }
        request['durations'] = {key: stamps[end] - stamps[start] if start in stamps and end in stamps else None
                                for key, (start, end) in pairs.items()}
        if any(value is not None and value < 0 for value in request['durations'].values()):
            raise ValueError('Nonmonotonic LLM phase timestamps')
    return list(requests.values())


def analyze(report):
    minimum = report['before']['min_heap']
    previous = None
    rounds = []
    for trial in report['trials']:
        telemetry_errors = []
        drops, timeline = [], []
        for event in trial['events']:
            if 'heap_time_ms' not in event:
                raise ValueError('This report lacks timestamped heap diagnostics')
            row = {key: event[key] for key in
                   ('stage', 'time_ms', 'heap_time_ms', 'free_heap', 'min_heap', 'largest_block')}
            for key in ('request', 'request_bytes', 'history_bytes'):
                if key in event:
                    row[key] = event[key]
            if 'text' in event:
                row['text'] = event['text']
            if row['min_heap'] < minimum:
                drops.append(dict(since_stage=previous['stage'] if previous else 'before_group',
                                  since_heap_time_ms=previous['heap_time_ms'] if previous else None,
                                  observed_stage=row['stage'], heap_time_ms=row['heap_time_ms'],
                                  previous_min=minimum, min_heap=row['min_heap']))
                minimum = row['min_heap']
            previous = row
            timeline.append(row)

        def last_text(stage):
            return next((e.get('text') for e in reversed(timeline) if e['stage'] == stage), None)

        def structured(stage):
            text = last_text(stage)
            if not text:
                return None
            try:
                value = json.loads(text)
                if isinstance(value, dict):
                    return value
                reason = 'Expected object telemetry'
            except (TypeError, ValueError) as error:
                reason = type(error).__name__ + ': ' + str(error)
            # Production variants may report a plain stage label. Keep that
            # evidence and the missing measurement, without inventing fields.
            telemetry_errors.append(dict(stage=stage, text=text, error=reason))
            return None

        radio = structured('capture_radio')
        restoration = [e for e in timeline if e['stage'] == 'capture_radio_restored']
        vad_ends = [e for e in timeline if e['stage'] == 'vad_end']
        radio_verified = None
        if radio is not None:
            expected = 8 if radio['rssi_dbm'] >= -55 and radio['previous'] > 8 else radio['previous']
            radio_verified = (radio['applied'] == expected and len(restoration) == 1
                              and restoration[0].get('text') == 'ok'
                              and all(restoration[0]['time_ms'] <= e['time_ms'] for e in vad_ends)
                              and report['after']['wifi_tx_quarter_dbm'] == report['before']['wifi_tx_quarter_dbm'])
        overlap=[]
        for attempt in trial['attempts']:
            for probe in attempt.get('ack_overlap_probes',[]):
                state=probe.get('response')
                active=(bool(state.get('playing')) and 0 <= state.get('rendered',-1) < state.get('total',0)) if state else None
                overlap.append(dict(stage=probe['stage'],event_time_ms=probe['event_time_ms'],
                                    query_latency_ms=(probe['observed_monotonic']-probe['requested_monotonic'])*1000 if state else None,
                                    audio_playing_with_queued_samples=active,
                                    rendered=state.get('rendered') if state else None,
                                    total=state.get('total') if state else None))
        rounds.append(dict(round=trial['round'], language=trial['language'], complete=trial['complete'],
                           first_wake=trial['attempts'][0]['triggered'], asr=trial.get('asr'),
                           error=last_text('error'), new_minima=drops,
                           capture_radio=radio, capture_radio_restoration_verified=radio_verified,
                           upload=structured('fast_upload_stats'),
                           native_fallback=last_text('progress_native_fallback'),
                           native_audio=structured('progress_native_stats'),
                           native_spool_sealed=structured('progress_buffered'),
                           native_spool_underruns=structured('progress_underruns'),
                           ack_overlap_probes=overlap,
                           llm_requests=llm_requests(trial['events']), timeline=timeline))
        rounds[-1]['telemetry_errors'] = telemetry_errors
    return dict(firmware=report['before']['version'], rounds=rounds,
                after=report['after'], limitation='Heap minima are cumulative since boot. Drops are bounded by observation intervals, not attributed to a specific allocator. Backdated DMA events use heap_time_ms for memory sampling. Board timestamps do not prove acoustic latency.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    path = args.directory / 'report.json'
    output = args.directory / 'stage-analysis.json'
    if output.exists():
        raise FileExistsError(output)
    raw = path.read_bytes()
    result = analyze(json.loads(raw))
    result['report_sha256'] = hashlib.sha256(raw).hexdigest()
    result['script_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps({**result, 'rounds': [{k: v for k, v in r.items() if k != 'timeline'}
                                        for r in result['rounds']]}, ensure_ascii=False))


if __name__ == '__main__':
    main()
