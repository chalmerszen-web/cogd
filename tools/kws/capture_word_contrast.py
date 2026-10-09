"""Thirty-two fixed TRAIN captures to fill a measured near-word coverage gap."""
import collections
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import wave

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'artifacts/kws-phase5'


def select(rows):
    voices = ('zf_xiaoni', 'zf_xiaoyi', 'zm_yunxi', 'zm_yunyang')
    sources = []
    for voice in voices:
        for language in ('zh', 'yue'):
            for label, text in ((1, '你好，小言'), (0, '你好小燕')):
                members = [r for r in rows if r['split'] == 'train' and r['source_group'] == 'kokoro-v1-' + voice
                           and r['language'] == language and r['label'] == label and r.get('text') == text]
                if not members:
                    raise ValueError(f'Missing paired source {voice}/{language}/{text}')
                # Select before acoustic/model results, stable source hash order.
                members.sort(key=lambda r: hashlib.sha256(r['clip_id'].encode()).hexdigest())
                sources.append(members[0])
    selected = []
    for row in sources:
        with wave.open(str(ROOT / row['path']), 'rb') as wav:
            seconds = wav.getnframes() / wav.getframerate()
        if seconds > 3.5:
            raise ValueError('Selected complete phrase exceeds capture window')
        for gain, tag in ((.35, 'normal'), (.35 * 10 ** (-12 / 20), 'minus12')):
            selected.append(dict(row, clip_id=row['clip_id'] + '-contrast-' + tag,
                                 parent_clip_id=row['clip_id'], playback_gain=gain,
                                 source_wav_sha256=hashlib.sha256((ROOT / row['path']).read_bytes()).hexdigest()))
    assert len(selected) == 32 and len({r['clip_id'] for r in selected}) == 32
    return selected


def main():
    from capture_corpus import validate_selection
    manifest = ROOT / 'artifacts/kws-phase3/dataset/manifest.jsonl'
    rows = [json.loads(line) for line in manifest.read_text(encoding='utf8').splitlines()]
    selected = select(rows)
    validate_selection(selected)
    parents = {r['clip_id']: r for r in rows}
    old = json.loads((OUT / 'features/features.json').read_text(encoding='utf8'))['captures']
    coverage = collections.Counter((parents[r['parent_clip_id']]['language'], parents[r['parent_clip_id']].get('text', ''))
                                   for r in old if r['split'] == 'train' and not parents[r['parent_clip_id']]['label'])
    assert coverage[('zh', '你好小燕')] == coverage[('yue', '你好小燕')] == 0
    destination = OUT / 'corpus-word-contrast'
    assert not destination.exists()
    selection = OUT / 'word-contrast-selection.json'
    with selection.open('x', encoding='utf8') as f:
        json.dump(selected, f, ensure_ascii=False, indent=2)
    plan = dict(max_captures=32, unique_sources=16, train_only=True, model_score_selection=False,
                manifest_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest(),
                selection_sha256=hashlib.sha256(selection.read_bytes()).hexdigest(),
                existing_captured_negative_coverage=[dict(language=k[0], text=k[1], count=v) for k, v in sorted(coverage.items())],
                levels_db=[0, -12], physical_five_metres=False, new_training_authorun=False)
    with (OUT / 'word-contrast-plan.json').open('x', encoding='utf8') as f:
        json.dump(plan, f, ensure_ascii=False, indent=2)
    command = [sys.executable, '-X', 'utf8', str(ROOT / 'tools/kws/capture_corpus.py'),
               '--out', str(destination), '--selection', str(selection), '--version', '0.7.0-xiaoyan-exp']
    with (OUT / 'capture-word-contrast.log').open('xb') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    if destination.exists():
        for name in ('capture_corpus.py', 'capture_word_contrast.py'):
            shutil.copyfile(ROOT / 'tools/kws' / name, destination / name)
    if result.returncode:
        raise RuntimeError('Contrast capture failed; preserve partial report, no automatic rerun')
    report = json.loads((destination / 'report.json').read_text(encoding='utf8'))
    assert report['complete'] and len(report['trials']) == 32
    print(json.dumps(dict(complete=True, captures=32, training_started=False)), flush=True)


if __name__ == '__main__':
    main()
