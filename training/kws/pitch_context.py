"""Append pitch to existing TRAIN windows from whole, stateful parent streams.

The recorded prefix, waveform and silence tail are replayed before slicing.
Original normalized C features must match for both whole parents and windows.
"""
import hashlib
import json
from pathlib import Path
import time
import wave

import numpy as np
from data import ROOT, normalize
from pitch_batch import BatchFrontend


def read(path):
    return json.loads(Path(path).read_text(encoding='utf8'))


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def arrays(path):
    # NpzFile does not cache decompressed arrays. Each repeated ['x'] access
    # would inflate the whole 175 MB TRAIN tensor for a single row comparison.
    with np.load(path, allow_pickle=False) as archive:
        return {key: archive[key] for key in archive.files}


def prepare(plan, out):
    started = time.monotonic()
    out = Path(out)
    out.mkdir(exist_ok=False)
    for name, expected in plan['hashes'].items():
        assert sha(ROOT/name) == expected, name
    files = plan['files']
    train = arrays(ROOT/files['train'])
    base = arrays(ROOT/files['base_train'])
    base_pitch = np.load(ROOT/files['base_pitch'], allow_pickle=False)
    assert train['x'].shape == (17066, 256, 40)
    assert base['x'].shape == (15791, 256, 40)
    assert base_pitch.shape == (15791, 256, 2) and base_pitch.dtype == np.int16
    for key in ('x', 'y', 'label', 'language', 'clip_id'):
        np.testing.assert_array_equal(train[key][:15791], base[key])
    # UX264 deliberately distinguishes observed windows from the entire base
    # family. It sets all base domain flags False, including old device crops.
    assert not train['device_domain'][:15791].any()
    features = np.zeros((17066, 256, 2), dtype=np.int16)
    features[:15791] = base_pitch
    filled = np.zeros(17066, bool)
    filled[:15791] = True
    stats = read(ROOT/files['normalization'])
    frontend = BatchFrontend(ROOT/files['batch_library'])
    forbidden = {json.loads(line)['source_group'] for line in
                 (ROOT/files['manifest']).read_text(encoding='utf8').splitlines()
                 if line.strip() and json.loads(line)['split'] != 'train'}
    actual = arrays(ROOT/files['actual'])
    public = arrays(ROOT/files['public'])
    windows = plan['windows']
    assert len(windows) == 1275
    assert [row['index'] for row in windows] == list(range(15791, 17066))
    parent_features = []
    provenance = []
    old_values = window_values = 0
    for parent in plan['parents']:
        assert time.monotonic()-started < plan['maximum_seconds']
        assert parent['split'] == 'train'
        assert parent['source_group'] not in forbidden
        path = ROOT/parent['path']
        assert path.as_posix().removeprefix(ROOT.as_posix()+'/') in plan['hashes']
        if parent['format'] == 'wav':
            with wave.open(str(path), 'rb') as source:
                assert (source.getnchannels(), source.getsampwidth(), source.getframerate()) == (1, 2, 16000)
                raw = source.readframes(source.getnframes())
        else:
            assert parent['format'] == 'pcm'
            raw = path.read_bytes()
        assert hashlib.sha256(raw).hexdigest() == parent['pcm_sha256']
        pcm = np.frombuffer(raw, dtype='<i2').astype(np.int16, copy=True)
        tail = 24000+(-len(pcm)-24000) % 512 if parent['data_key'] == 'natural' else 0
        signal = np.r_[np.zeros(32768, np.int16), pcm, np.zeros(tail, np.int16)]
        assert len(signal) % 256 == 0
        logmel, pitch = frontend(signal)
        x = normalize(logmel, stats)
        key, index = parent['data_key'], parent['source_index']
        if key == 'natural':
            reference = np.load(ROOT/parent['features'], allow_pickle=False)['x']
            length = len(reference)
        else:
            source = actual if key == 'actual' else public
            reference = source['x'][index]
            length = int(source['length'][index])
            assert parent['label'] == int(source['label'][index])
            assert parent['language'] == str(source['language'][index])
        assert len(x) == length, (key, index, len(x), length)
        np.testing.assert_array_equal(x, reference[:length])
        assert not reference[length:].any()
        old_values += x.size
        offset = sum(len(value) for value in parent_features)
        parent_features.append(pitch)
        source_windows = [row for row in windows if row['data_key'] == key and row['source_index'] == index]
        assert source_windows, (key, index)
        for row in source_windows:
            at, begin, real = row['index'], row['start_frame'], row['real_frames']
            assert not filled[at]
            assert real == min(256, length-begin) and real > 128
            assert train['length'][at] == real
            assert train['label'][at] == row['label'] == parent['label']
            assert str(train['language'][at]) == row['language'] == parent['language']
            np.testing.assert_array_equal(train['x'][at, :real], x[begin:begin+real])
            assert not train['x'][at, real:].any()
            features[at, :real] = pitch[begin:begin+real]
            filled[at] = True
            window_values += real*40
        provenance.append(dict(parent, full_frames=length, prefix_frames=128,
                               tail_samples=tail, pitch_offset=offset, windows=len(source_windows)))
        if len(provenance) % 32 == 0:
            print(json.dumps(dict(parents=len(provenance), windows=int(filled.sum()-15791),
                                  seconds=time.monotonic()-started)), flush=True)
    assert filled.all(), np.flatnonzero(~filled).tolist()
    assert len(provenance) == 231
    np.save(out/'pitch.npy', features)
    np.save(out/'parent-pitch.npy', np.vstack(parent_features))
    np.testing.assert_array_equal(np.load(out/'pitch.npy')[:15791], base_pitch)
    (out/'parent-provenance.json').write_text(json.dumps(provenance, separators=(',', ':'))+'\n', encoding='utf8')
    (out/'window-provenance.json').write_text(json.dumps(windows, separators=(',', ':'))+'\n', encoding='utf8')
    for name, expected in plan['hashes'].items():
        assert sha(ROOT/name) == expected, name
    report = dict(complete=True, rows=17066, base_rows=15791, continuous_windows=1275,
                  full_parents=231, old_parent_input_values_exact=old_values,
                  old_window_input_values_exact=window_values, pitch_values=features.size,
                  parent_pitch_frames=sum(len(value) for value in parent_features),
                  original_TRAIN_labels_inputs_unchanged=True,
                  causal_parent_state_preserved=True, no_DEV_TEST_waveform_read=True,
                  pitch_sha256=sha(out/'pitch.npy'), parent_pitch_sha256=sha(out/'parent-pitch.npy'),
                  seconds=time.monotonic()-started, trained_model=False)
    (out/'audit.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
    return report
