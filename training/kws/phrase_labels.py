"""TRAIN-only full-phrase identity labels, from original source boundaries.

The eight classes are training supervision, not an on-device decision table.
Fragments, ambient tails, unlisted speech, and continuous windows stay ignored.
All original PCM recipes, binary labels, and split identities are retained.
"""
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import time

import numpy as np

import pitch_replay as replay
from data import ROOT
from negative_groups import NEAR_WORDS

CLASSES = ('你好小言', *sorted(NEAR_WORDS))
CONTRACT = 'full_phrase_contrast8_penultimate24_tail160ms_train_only_v1'
IGNORE = -1


def normalized(text):
    return ''.join(c for c in text if c not in ' ,，。!！?？')


def phrase_class(row):
    text = normalized(str(row.get('text', '')))
    if row['label'] == 1:
        if text != CLASSES[0]:
            raise ValueError('Positive transcript differs from the wake phrase')
        return 0
    if row.get('sampling_group') == 'hard_negative' and text in NEAR_WORDS:
        return CLASSES.index(text)
    return IGNORE


class Placement:
    """Copy the RNG before its placement draw; never advance the recipe RNG."""
    def __init__(self, original):
        self.original, self.last = original, None

    def __call__(self, row, pcm, rng=None, frames=256, background=None):
        self.last = None
        if row.get('endpoint_schema') == 1:
            low, high = row['speech_end_low_sample'], row['speech_end_high_sample']
            earliest = max(4096, 33792-low)
            latest = frames*256-len(pcm)-1024
            if latest < earliest:
                raise ValueError('Original whole phrase cannot fit')
            if rng is None:
                offset = (earliest+latest)//2
            else:
                bit_generator = type(rng.bit_generator)()
                bit_generator.state = copy.deepcopy(rng.bit_generator.state)
                offset = int(np.random.Generator(bit_generator).integers(earliest, latest+1))
            self.last = dict(text=normalized(str(row.get('text', ''))),
                             label=row['label'], low=offset+low, high=offset+high,
                             source_clip_id=row['clip_id'])
        return self.original(row, pcm, rng, frames, background)


class Recorder:
    def __init__(self, train, stats, library, out, maximum_seconds):
        # Signature intentionally matches the frozen replay Recorder.
        self.began, self.maximum_seconds = time.monotonic(), maximum_seconds
        self.out = Path(out)
        self.out.mkdir(exist_ok=False)
        with np.load(train, allow_pickle=False) as source:
            self.old = {k: source[k] for k in source.files if k != 'x'}
        proof = replay.read(ROOT/'artifacts/voice-fast/wake-periodicity-corpus-ux413/base-features/row-provenance.json')
        self.proof = {r['index']: r for r in proof}
        self.indices = {key: i for i, key in enumerate(zip(self.old['clip_id'].tolist(), self.old['variant'].tolist()))}
        assert len(self.indices) == len(self.proof) == 15791
        self.filled = np.zeros(15791, dtype=bool)
        self.classes = np.full(17066, IGNORE, dtype=np.int8)
        self.ends = np.full(17066, IGNORE, dtype=np.int32)
        self.mask = np.zeros((17066, 256), dtype=bool)
        self.counts, self.audit = Counter(), []

    def add(self, family, row, variant, signal, labels, end, device=False):
        if time.monotonic()-self.began > self.maximum_seconds:
            raise TimeoutError('Finite phrase annotation replay exceeded')
        i = self.indices[row['clip_id'], variant]
        assert not self.filled[i]
        for key in ('label', 'language', 'source_group'):
            assert row[key] == self.old[key][i], (key, i)
        assert bool(device) == bool(self.old['device_domain'][i])
        np.testing.assert_array_equal(labels, self.old['y'][i])
        assert self.old['event_end'][i] == (IGNORE if end is None else end)
        actual = hashlib.sha256(signal.astype('<i2').tobytes()).hexdigest()
        proof = self.proof[i]
        assert (proof['clip_id'], proof['variant'], proof['family'], proof['PCM_sha256']) == (row['clip_id'], variant, family, actual), i
        self.filled[i] = True
        self.counts[family] += 1
        kind = phrase_class({key: self.old[key][i].item() for key in ('label', 'text', 'sampling_group')})
        if kind == IGNORE:
            return
        placement = self.placement.last
        if placement is None or row['clip_id'].endswith('-ambient'):
            raise ValueError('Full-phrase label lacks original source placement')
        assert placement['label'] == row['label'] and placement['text'] == CLASSES[kind], i
        low, high = placement['low'], placement['high']
        assert 32768 <= low <= high <= 65536, i
        if kind == 0:
            assert high == end, i
        frame_ends = (np.arange(256)+1)*256
        selected = (frame_ends >= high) & (frame_ends <= high+2560)
        # The mask already intersects the original materialized256 frames.
        # Original near-tail placements need not have a full160ms suffix.
        assert 1 <= selected.sum() <= 11
        self.classes[i], self.ends[i], self.mask[i] = kind, high, selected
        self.audit.append(dict(index=i, class_id=kind, language=str(self.old['language'][i]),
                               source_group=str(self.old['source_group'][i]),
                               source_clip_id=placement['source_clip_id'], source_end_interval=[low, high],
                               PCM_sha256=actual))

    def checkpoint(self, family):
        print(json.dumps(dict(family=family, rows=int(self.filled.sum()), labeled=int((self.classes >= 0).sum()), seconds=time.monotonic()-self.began)), flush=True)

    def finish(self):
        assert self.filled.all(), 'Original TRAIN row missing'
        expected = np.array([phrase_class({key: self.old[key][i].item() for key in ('label', 'text', 'sampling_group')}) for i in range(15791)], dtype=np.int8)
        np.testing.assert_array_equal(expected, self.classes[:15791])
        assert np.all(self.classes[15791:] == IGNORE) and not self.mask[15791:].any()
        assert np.array_equal(self.mask.any(1), self.classes >= 0)
        counts = {name: {language: int(np.count_nonzero((self.classes[:15791] == k) & (self.old['language'] == language))) for language in ('zh', 'yue')} for k, name in enumerate(CLASSES)}
        assert all(n >= 16 for row in counts.values() for n in row.values()), counts
        assert int(np.count_nonzero(self.classes > 0)) == 796
        np.savez_compressed(self.out/'labels.npz', class_id=self.classes, end=self.ends, mask=self.mask)
        (self.out/'provenance.json').write_text(json.dumps(self.audit, ensure_ascii=False, separators=(',', ':'))+'\n', encoding='utf8')
        result = dict(complete=True, passed=True, contract=CONTRACT, classes=CLASSES,
                      rows=17066, replayed_rows=15791, labeled=int((self.classes >= 0).sum()), counts=counts,
                      masked_frames=int(self.mask.sum()), all_original_PCM_sha256_exact=True,
                      original_binary_labels_ends_splits_unchanged=True, source_energy_bounds_not_phonetic_alignment=True,
                      no_DEV_TEST_audio_fit_USB_Flash=True, seconds=time.monotonic()-self.began)
        (self.out/'audit.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
        return result


def prepare(plan, out):
    placement = Placement(replay.framed_example)
    old_recorder, old_framed = replay.Recorder, replay.framed_example
    def factory(*args, **kwargs):
        recorder = Recorder(*args, **kwargs)
        recorder.placement = placement
        return recorder
    try:
        replay.Recorder, replay.framed_example = factory, placement
        return replay.prepare(plan, out)
    finally:
        replay.Recorder, replay.framed_example = old_recorder, old_framed
