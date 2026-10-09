"""Extract pitch for fixed TRAIN examples after reconstructing old C inputs.

Produces a separate feature archive. It neither edits existing examples nor
trains a classifier; source/augmentation identity must be verified first.
"""
import hashlib
import json
from pathlib import Path
import numpy as np
from data import ROOT, Frontend, framed_example, load_pcm, normalize
from pitch_frontend import Pitch


def prepare(rows, original_train, normalization, pitch_library, out):
    if not rows or len({r['clip_id'] for r in rows}) != len(rows):
        raise ValueError('Missing or duplicate fixed sources')
    if any(r['split'] != 'train' or r['label'] != 1 for r in rows):
        raise ValueError('This source contract accepts TRAIN positives only')
    out = Path(out)
    if out.exists():
        raise ValueError('Never replace a prior feature run')
    out.mkdir()
    stats = json.loads(Path(normalization).read_text(encoding='utf8'))
    frontend, pitch = Frontend(), Pitch(pitch_library)
    with np.load(original_train, allow_pickle=False) as saved:
        # Only metadata and the selected unaugmented rows are materialized.
        clip_ids, variants = saved['clip_id'], saved['variant']
        labels, groups = saved['label'], saved['source_group']
        indices = []
        for row in rows:
            matches = np.flatnonzero((clip_ids == row['clip_id']) & (variants == 0))
            if len(matches) != 1:
                raise ValueError('Unaugmented TRAIN membership not unique: '+row['clip_id'])
            index = int(matches[0])
            if labels[index] != 1 or groups[index] != row['source_group']:
                raise ValueError('TRAIN source identity mismatch')
            indices.append(index)
        old_x = saved['x'][indices]
        old_y = saved['y'][indices]
        old_end = saved['event_end'][indices]
    features, observations = [], []
    for position, row in enumerate(rows):
        signal = load_pcm(row)
        pcm, y, end = framed_example(row, signal)
        x = normalize(frontend(pcm), stats)
        np.testing.assert_array_equal(x, old_x[position])
        np.testing.assert_array_equal(y, old_y[position])
        assert end == old_end[position]
        value = pitch(pcm)
        assert value.shape == (len(x),2)
        assert np.all((value[:,0] == 0) == (value[:,1] == 0))
        voiced = value[:,0] > 0
        assert np.all((value[voiced,0] >= 1280) & (value[voiced,0] <= 12800))
        assert np.all((value[voiced,1] >= 3481) & (value[voiced,1] <= 4096))
        features.append(value)
        observations.append(dict(clip_id=row['clip_id'],source_group=row['source_group'],language=row['language'],
            train_index=indices[position],PCM_sha256=hashlib.sha256(pcm.astype('<i2').tobytes()).hexdigest(),
            voiced_frames=int(np.count_nonzero(voiced)),total_frames=len(value),
            median_Hz=float(np.median(value[voiced,0])/16) if np.any(voiced) else None))
    np.savez_compressed(out/'pitch.npz',index=np.array(indices,dtype=np.int64),pitch=np.stack(features),
        clip_id=np.array([r['clip_id'] for r in rows]),source_group=np.array([r['source_group'] for r in rows]),
        language=np.array([r['language'] for r in rows]))
    with np.load(out/'pitch.npz',allow_pickle=False) as written:
        np.testing.assert_array_equal(written['pitch'],np.stack(features))
        np.testing.assert_array_equal(written['index'],indices)
    report=dict(complete=True,sources=len(rows),old_C_input_values_exact=int(old_x.size),
        features_shape=list(np.stack(features).shape),rows=observations,
        all_labels_endpoints_and_original_features_preserved=True,TRAIN_positive_only=True,
        no_phonetic_or_classification_quality_claim=True,no_new_fit=True)
    (out/'audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    return report
