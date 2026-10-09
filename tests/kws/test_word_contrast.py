"""Measured contrast collection must never fall back to validation/test voices."""
import sys
from pathlib import Path
import wave

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/kws'))
import capture_word_contrast as capture
from capture_corpus import validate_selection


def test_contrast_keeps_paired_training_sources(tmp_path, monkeypatch):
    monkeypatch.setattr(capture, 'ROOT', tmp_path)
    with wave.open(str(tmp_path / 'fixture.wav'), 'wb') as stream:
        stream.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
        stream.writeframes(bytes(3200))
    rows = []
    for voice in ('zf_xiaoni', 'zf_xiaoyi', 'zm_yunxi', 'zm_yunyang'):
        for language in ('zh', 'yue'):
            for label, text in ((1, '你好，小言'), (0, '你好小燕')):
                row = dict(clip_id=f'{voice}-{language}-{label}', source_group='kokoro-v1-' + voice,
                           language=language, label=label, text=text, split='train', path='fixture.wav')
                rows.extend([row, dict(row, clip_id='forbidden-' + row['clip_id'], split='test')])
    selected = capture.select(rows)
    validate_selection(selected)
    assert len(selected) == 32
    assert len({r['parent_clip_id'] for r in selected}) == 16
    assert all(not r['parent_clip_id'].startswith('forbidden-') for r in selected)
    for source in {r['parent_clip_id'] for r in selected}:
        pair = [r for r in selected if r['parent_clip_id'] == source]
        assert sorted(r['playback_gain'] for r in pair) == [.35 * 10 ** (-12 / 20), .35]
    # Removing a required TRAIN source cannot be repaired by its TEST twin.
    with pytest.raises(ValueError, match='Missing paired source'):
        capture.select(rows[1:])


def test_validation_comparison_cannot_become_training_capture():
    rows=[dict(clip_id='held-out',split='validation',playback_gain=.35)]
    validate_selection(rows,comparison=True)
    with pytest.raises(ValueError):validate_selection(rows)
    with pytest.raises(ValueError):validate_selection(rows,diagnostic=True)
    with pytest.raises(ValueError):validate_selection(rows,diagnostic=True,comparison=True)
    with pytest.raises(ValueError):validate_selection([dict(rows[0],split='train')],comparison=True)
