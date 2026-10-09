"""Compare one retained raw/HP300 ASR observation; never rerun a model."""
import argparse
import hashlib
import json
from pathlib import Path

from asr_vad_probe import PREPARED_IDS


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf8'))


def compare(directory):
    target = directory/'paired-comparison.json'
    if target.exists():
        raise FileExistsError(target)
    report = read(directory/'report.json')
    audit = read(directory/'offline-audit.json')
    inputs = read(directory/'source-report.json')
    assert audit['source_report_sha256'] == sha(directory/'report.json')
    assert audit['traffic_sha256'] == sha(directory/'traffic.jsonl')
    assert audit['observe_failures'] and audit['evidence_consistent'] and not audit['device_acceptance']
    assert report['source_report_sha256'] == sha(directory/'source-report.json')
    assert inputs['cloud_prerequisites_pass'] is True
    assert [f['id'] for f in report['fixtures']] == PREPARED_IDS
    assert len(report['trials']) == len(audit['trials']) == 6
    rows = []
    for number, (f, t, checked, source) in enumerate(zip(report['fixtures'], report['trials'], audit['trials'], inputs['fixtures']), 1):
        assert number == t['round'] == checked['round']
        assert all(f[k] == source[k] for k in ('id', 'samples', 'pcm_sha256', 'original_samples', 'extra_tail_ms'))
        assert t['sent_samples'] == checked['samples'] == f['samples']
        segments = checked['segments']
        nonempty = [s for s in segments if s['text']]
        assert nonempty
        last = nonempty[-1]
        rows.append(dict(id=f['id'], text=t['combined_text'], text_complete=checked['transcript_complete'],
            segments=len(segments), empty_segments=checked['empty_final_segments'],
            samples=f['samples'], original_ms=f['original_samples']/16,
            last_nonempty_end_ms=last['end_ms'], last_nonempty_final_before_finish=last['final_before_finish'],
            endpoint_before_appended_tail=last['end_ms'] <= f['original_samples']/16,
            final_from_input_start_ms=round((last['final_s']-t['input_start_s'])*1000),
            source_order_ids=[s['item_id'] for s in segments]))
    pairs = []
    for at in range(0, 6, 2):
        raw, hp = rows[at:at+2]
        pairs.append(dict(id=raw['id'].removesuffix('-raw'), raw=raw, filtered=hp,
            source_endpoint_change_ms=hp['last_nonempty_end_ms']-raw['last_nonempty_end_ms'],
            final_wall_change_ms=hp['final_from_input_start_ms']-raw['final_from_input_start_ms']))
    result = dict(device_acceptance=False, adopted=False, pairs=pairs,
        hashes={name:sha(directory/name) for name in ('report.json', 'offline-audit.json', 'traffic.jsonl', 'source-report.json')},
        script_sha256=sha(Path(__file__)),
        rationale='No consistent endpoint improvement; same incomplete transcripts and added empty segments. Do not adopt.',
        limitations=['One pair per retained synthetic-source physical clip, fixed sequential order, no repeated or randomized inference.',
            'Source endpoints in appended zero padding cannot demonstrate device noise suppression.',
            'Literal input fidelity rejects omitted or homophone-substituted characters; no relaxed pass criterion.',
            'Current C collector rejects empty final segments; observation audit does not certify C acceptance.',
            'No new microphone capture, device flash, effect or response-speed measurement.'])
    target.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    print(json.dumps(dict(adopted=False, pairs=pairs), ensure_ascii=False))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    compare(p.parse_args().directory)
