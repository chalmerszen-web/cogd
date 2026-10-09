"""Summarize fixed-ring allocation observations, without declaring full tracing."""
import argparse
import hashlib
import json
from pathlib import Path


def analyze(report):
    rows = []
    for trial in report['trials']:
        observations = [json.loads(e['text']) for e in trial['events'] if e['stage'] == 'capture_heap']
        summaries = [e for e in trial['events'] if e['stage'] == 'capture_heap_summary']
        if len(summaries) != 1:
            rows.append(dict(round=trial['round'], available=False, reason='Missing or repeated capture summary'))
            continue
        event = summaries[0]
        summary = json.loads(event['text'])
        expected = list(range(summary['overwritten'] + 1, summary['minima'] + 1))
        complete_ring = [e['seq'] for e in observations] == expected
        if complete_ring and observations:
            assert observations[-1]['free'] == summary['minimum']
            assert all(b['free'] < a['free'] for a, b in zip(observations, observations[1:]))
            assert all(b['at_ms'] >= a['at_ms'] for a, b in zip(observations, observations[1:]))
        rows.append(dict(round=trial['round'], available=complete_ring, capture=summary,
                         cumulative_heap_min_at_report=event['min_heap'], retained_minima=observations,
                         first_wake=trial['attempts'][0]['triggered'], completed=trial['complete']))
    return dict(firmware=report['before']['version'], rounds=rows,
                limitations=[
                    'Diagnostic firmware changes memory and timing; not a production speed test.',
                    'Minimum is observed total free bytes at retained successful allocation callbacks.',
                    'Skipped callbacks and overwritten rows are reported, not reconstructed.',
                    'SDK cumulative minimum can combine different heap regions at different times.',
                    'Task and triggering allocation identify an observation, not all outstanding allocations or a leak cause.'
                ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    raw = (args.directory / 'report.json').read_bytes()
    out = args.directory / 'heap-analysis.json'
    if out.exists():
        raise FileExistsError(out)
    result = analyze(json.loads(raw))
    result['report_sha256'] = hashlib.sha256(raw).hexdigest()
    result['script_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    out.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps({**result, 'rounds': [{k: v for k, v in r.items() if k != 'retained_minima'}
                                        for r in result['rounds']]}, ensure_ascii=False))


if __name__ == '__main__':
    main()
