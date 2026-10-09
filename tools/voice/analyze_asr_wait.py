"""Separate transport wall-time from the manual final-ASR wait; no CPU claims."""
import argparse
import hashlib
import json
from pathlib import Path


def analyze(report):
    rows = []
    for trial in report['trials']:
        events = trial['events']
        def one(stage):
            found = [e for e in events if e['stage'] == stage]
            return found[0] if len(found) == 1 else None
        joined, final, cached, measured = [one(s) for s in
            ('vad_end', 'prefetch_input', 'asr_cached', 'asr_ws')]
        row = dict(round=trial['round'], complete_input=trial['input_complete'],
                   task_complete=trial['core_task_complete'], available=False)
        if all((joined, final, cached, measured)):
            counters = json.loads(measured['text'])
            elapsed = cached['time_ms']-joined['time_ms']
            valid = elapsed > 0 and all(isinstance(counters.get(k), list) and
                len(counters[k]) == 3 and all(isinstance(v, int) and v >= 0 for v in counters[k])
                for k in ('tx', 'rx', 'wp', 'rp', 'wr', 'rd', 'rd0'))
            if valid:
                io = counters['tx'][1]+counters['rx'][1]
                row.update(available=True, interval_ms=elapsed,
                    final_asr_after_join_ms=final['time_ms']-joined['time_ms'],
                    cached_samples_at_final_asr=json.loads(cached['text']),
                    transport_calls_wall_ms=io, other_wall_ms=elapsed-io,
                    transport_wall_fraction=io/elapsed, counters=counters)
        rows.append(row)
    return dict(version=report['after']['version'], diagnostic=True, accepted=False,
                rows=rows, min_heap=report['after']['min_heap'],
                limitations=[
                    'Wall-time includes blocking, TLS processing and task preemption; not exclusive CPU.',
                    'rp/rd/wp/wr are nested inside rx/tx; never add all counters together.',
                    'Interval starts at legacy vad_end after capture/upload join, not microphone stop.',
                    'Cached samples include audio received during capture; not the interval byte count.',
                    'Other wall-time includes parsing, cache encoding, callbacks and measurement overhead.',
                    'A transport wait alone cannot distinguish server delay, link delay and flow control.',
                    'Diagnostic timing cannot establish normal-firmware acoustic acceptance.'
                ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    target = args.directory/'asr-wait-analysis.json'
    if target.exists():
        raise FileExistsError(target)
    data = (args.directory/'report.json').read_bytes()
    result = analyze(json.loads(data))
    result['report_sha256'] = hashlib.sha256(data).hexdigest()
    result['script_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    target.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
