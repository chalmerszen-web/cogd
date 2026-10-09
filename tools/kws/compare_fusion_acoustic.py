"""Summarize retained physical regressions without changing acceptance rules."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'artifacts/kws-phase5'
FUSION = BASE / 'fusion-ef-smooth3'


def summarize(path):
    value = json.loads(path.read_text(encoding='utf8'))
    assert value['complete'] and len(value['trials']) == sum(
        x['total'] for x in value['languages'].values()) + value['negatives']['total']
    tails = [t.get('observed_after_playback_ms') for t in value['trials']]
    tail_verified = all(t is not None and t >= 1200 for t in tails)
    if FUSION in path.parents:
        assert tail_verified
    partial = [t for t in value['trials'] if not t['label'] and t['text'] in ('你好', '小言')]
    failures = [dict(clip_id=t['clip_id'], language=t['language'], text=t['text'],
                     label=t['label'], triggered=t['triggered'], early=t['early'])
                for t in value['trials'] if (t['label'] and not t['valid_hit'])
                or (not t['label'] and t['triggered'])]
    listening = [s['status'] for s in value['samples'] if s['wake']['state'] == 'listening']
    negative = value['negatives']
    gates = dict(recall=all(x['valid_hits'] / x['total'] >= .9 for x in value['languages'].values()),
                 negative_rate=negative['triggers'] / negative['total'] <= .05,
                 no_partial=not any(t['triggered'] for t in partial),
                 resources=not value['resources']['failures'])
    return dict(path=path.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                settings=value['settings'], languages=value['languages'], negatives=negative,
                partial=dict(total=len(partial), triggers=sum(t['triggered'] for t in partial)),
                resources=value['resources'], gates=gates, failures=failures,
                recorded_tail_verified=tail_verified,
                listening_heap_min=min(s['free_heap'] for s in listening),
                listening_largest_block_min=min(s['largest_block'] for s in listening),
                source_files=value['source_files'])


def main():
    comparison = dict(five_metres_verified=False, human_generalization_verified=False,
                      interpretation='Repeated-source regression in sequential sessions; no calibrated SPL/distance or controlled ambient equivalence.',
                      runs={})
    for name in ('near-formal', 'weak-minus06', 'weak-minus12', 'weak-minus18', 'weak-minus24'):
        new = FUSION / name / 'replay.json'
        if not new.exists() or not json.loads(new.read_text(encoding='utf8'))['complete']:
            continue
        row = dict(fusion=summarize(new))
        previous = BASE / name / 'replay.json'
        if previous.exists():
            row['baseline_b'] = summarize(previous)
            row['same_source_files'] = row['baseline_b']['source_files'] == row['fusion']['source_files']
            assert row['same_source_files'], name
        comparison['runs'][name] = row
    (FUSION / 'acoustic-comparison.json').write_text(json.dumps(comparison, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    print(json.dumps({name: {k: v for k, v in row['fusion'].items() if k in ('languages', 'negatives', 'gates', 'partial', 'resources')}
                      for name, row in comparison['runs'].items()}, ensure_ascii=False))


if __name__ == '__main__':
    main()
