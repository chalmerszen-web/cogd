"""Keep unblinded diagnostic recordings out of the TRAIN capture route."""
import sys
from pathlib import Path
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/kws'))
from capture_corpus import validate_selection


def rows(split, count=1):
    return [dict(clip_id=str(i), split=split, playback_gain=.35) for i in range(count)]


def test_default_never_accepts_test_sources():
    validate_selection(rows('train', 64))
    with pytest.raises(ValueError):
        validate_selection(rows('test'))


def test_diagnostic_is_separate_and_finite():
    validate_selection(rows('test', 10), diagnostic=True)
    for selection in (rows('test', 11), rows('train'), rows('validation')):
        with pytest.raises(ValueError):
            validate_selection(selection, diagnostic=True)


def test_repeated_ids_and_unsafe_gain_rejected():
    for selection in (rows('test') * 2, [dict(clip_id='a', split='test', playback_gain=.8)]):
        with pytest.raises(ValueError):
            validate_selection(selection, diagnostic=True)
