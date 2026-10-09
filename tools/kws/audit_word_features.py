"""Source/label invariants before measured contrast training."""
import hashlib
import json
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[2]


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    base=ROOT/'artifacts/kws-phase5/features-measured-v2'
    folder=ROOT/'artifacts/kws-phase5/features-word-contrast'
    original=np.load(base/'train.npz',allow_pickle=False)
    current=np.load(folder/'train.npz',allow_pickle=False)
    n=len(original['x']);extra=len(current['x'])-n
    assert 1<=extra<=300
    for key in original.files:np.testing.assert_array_equal(current[key][:n],original[key])
    unchanged={name:sha(base/name) for name in ('validation.npz','test.npz','normalization.json')}
    assert all(sha(folder/name)==value for name,value in unchanged.items())
    validation=np.load(folder/'validation.npz',allow_pickle=False)
    reserved=np.load(folder/'test.npz',allow_pickle=False)
    assert not set(current['source_group'])&(set(validation['source_group'])|set(reserved['source_group']))
    assert current['device_domain'][n:].all()
    metadata=json.loads((folder/'features.json').read_text(encoding='utf8'))
    assert metadata['captures']==28 and len(metadata['ancestry'])==28
    with (folder/'audit.json').open('x') as f:
        json.dump(dict(passed=True,original_examples=n,added_examples=extra,
                       original_train_prefix_identical=True,unchanged=unchanged,
                       source_groups_separated=True,test_audio_or_scores_read=False,
                       train_sha256=sha(folder/'train.npz')),f,indent=2)
    print(json.dumps(dict(passed=True,original=n,added=extra)))


if __name__=='__main__':main()
