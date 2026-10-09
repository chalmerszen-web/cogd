"""Frozen E/F baseline on new TRAIN contrast recordings; not a quality gate."""
import hashlib
import json
from pathlib import Path
import sys
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from append_captures import capture_path
from device_domain import pcm
from evaluate import events
from diagnose_fusion_captures import score


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    base = ROOT / 'artifacts/kws-phase5'
    output = base / 'word-contrast-baseline.json'
    assert not output.exists()
    corpus = base / 'corpus-word-contrast/report.json'
    audit_path = base / 'word-contrast-audit.json'
    report = json.loads(corpus.read_text(encoding='utf8'))
    audit = json.loads(audit_path.read_text(encoding='utf8'))
    assert audit['source_report_sha256'] == sha(corpus)
    assert report['complete'] and len(report['trials']) == 32
    accepted = {r['clip_id']: r for r in audit['trials'] if r['eligible']}
    library = ROOT / 'build-kws-fusion-host/libkws.so'
    parity = json.loads((base / 'fusion-ef-smooth3/fusion-host-parity.json').read_text())
    assert sha(library) == parity['library_sha256']
    threshold = json.loads((base / 'fusion-ef-smooth3/threshold.json').read_text())['quantized']
    assert threshold == 281
    warmup, trials = 32768, []
    for t in report['trials']:
        row = t['source']
        if row['clip_id'] not in accepted:
            continue
        assert row['split'] == 'train'
        a = accepted[row['clip_id']]
        signal = pcm(capture_path(t['path']))
        assert hashlib.sha256(signal.tobytes()).hexdigest() == t['signal']['sha256']
        values = score(np.concatenate([np.resize(signal[:4096], warmup), signal]), library)
        detected = [x - warmup for x in events(values, threshold) if x >= warmup]
        low = a['lag_samples'] + row['speech_end_low_sample'] - 320 if row['label'] else None
        high = a['lag_samples'] + row['speech_end_high_sample'] + 320 if row['label'] else None
        trials.append(dict(clip_id=row['clip_id'], parent_clip_id=row['parent_clip_id'],
            language=row['language'], text=row['text'], label=row['label'], source_group=row['source_group'],
            gain=row['playback_gain'], events_samples=detected, accept=[low, high],
            valid_hit=bool(row['label'] and any(low <= x <= high + 12800 for x in detected)),
            premature=sum(x < low for x in detected) if row['label'] else 0,
            peak_logit_q8=int(values[128:].max())))
    summary = {}
    for language in ('zh', 'yue'):
        summary[language] = {}
        for gain in (.35, .35 * 10 ** (-12 / 20)):
            selected = [t for t in trials if t['language'] == language and t['gain'] == gain]
            summary[language][str(gain)] = dict(positive=sum(t['label'] for t in selected),
                hits=sum(t['valid_hit'] for t in selected), negative=sum(not t['label'] for t in selected),
                false_clips=sum(not t['label'] and bool(t['events_samples']) for t in selected),
                premature=sum(t['premature'] for t in selected))
    assert sha(library) == parity['library_sha256']
    with output.open('x', encoding='utf8') as f:
        json.dump(dict(complete=True, threshold_q8=threshold, selected=len(trials),
            library_sha256=parity['library_sha256'], corpus_sha256=sha(corpus),
            audit_sha256=sha(audit_path), script_sha256=sha(Path(__file__)),
            summary=summary, trials=trials, trained=False, flashed=False,
            limitation='TRAIN development baseline. Repeat first256ms as2.048s warmup; not live listener state, new blind evaluation or physical distance.'),
            f, ensure_ascii=False, indent=2)
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
