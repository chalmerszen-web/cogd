"""Audit frozen source identities and PCM against ancestral and added TRAIN."""
import hashlib, json, sys
from pathlib import Path
import numpy as np
from wake_prepare import pcm

ROOT = Path(__file__).resolve().parents[2]
P = ROOT / 'ESP32-P4-WIFI6-Touch-LCD-3.5'
B = P / 'wake_training/20261101'
T = B / 'three-rounds'
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def lines(path): return [json.loads(line) for line in path.read_text(encoding='utf8').splitlines()]
def main():
    tests = lines(T / 'combined-test.jsonl')
    forbidden = ('longanfengyue', 'xunanchuan')
    checks = []
    ancestry = json.loads((B/'round2-pretrained-ancestry.json').read_text(encoding='utf8'))
    feature_paths = {ROOT/r['features'] for r in ancestry['checkpoints']}
    feature_paths.update(B/name for name in ('features-v10','features-v12b'))
    feature_paths.update(T/('features-r%d'%i) for i in (1,2,3))
    for path in sorted(feature_paths):
        with np.load(path/'train.npz', allow_pickle=False) as data:
            columns = [k for k in data.files if data[k].dtype.kind in 'US']
            assert 'clip_id' in columns or 'source_group' in columns
            strings = {str(s) for k in columns for s in data[k]}
            assert not any(word in s for s in strings for word in forbidden)
            assert not any(r['clip_id'] in s for r in tests for s in strings)
            checks.append({'features':path.relative_to(ROOT).as_posix(), 'train_sha256':sha(path/'train.npz'),
                           'string_columns':columns, 'forbidden_matches':0})
    # Include even retired development/test audio conservatively; these can be
    # ancestral training in later checkpoints. Same native voice is allowed,
    # identical audio or a matching recording id is not.
    training = []
    for name in ('combined-manifest.jsonl','expanded-train-round1/manifest.jsonl',
                 'expanded-train-round2/manifest.jsonl','qwen-native-joint-train/manifest.jsonl',
                 'native-joint-augmented/manifest.jsonl','heldout-physical-manifest.jsonl',
                 'heldout-round2-manifest.jsonl'):
        training += lines(B/name)
    training += lines(T/'combined-train.jsonl')
    for path in (B/'live-training-input-v11/report.json', T/'live-low-010/report.json'):
        report=json.loads(path.read_text(encoding='utf8'));assert report['complete']
        training += [row['source'] for row in report['trials']]
        training += [{'path':row['path']} for row in report['trials']]
    paths={str(ROOT/row['path']) for row in training}
    known={hashlib.sha256(pcm(path).tobytes()).hexdigest() for path in paths}
    test_hashes=[hashlib.sha256(pcm(row['path']).tobytes()).hexdigest() for row in tests]
    assert len(set(test_hashes))==len(test_hashes)
    assert not set(test_hashes)&known
    result={'complete':True,'training_checks':checks,'known_recordings_checked':len(paths),
            'test_recordings':len(tests),'new_recording_overlap':0,'forbidden_voice_matches':0,
            'combined_test_sha256':sha(T/'combined-test.jsonl'),
            'frozen_manifest_sha256':sha(T/'frozen-stress/manifest.jsonl'),
            'script_sha256':sha(Path(__file__)),
            'scope':'Mandarin voices absent from ancestral and all three rounds TRAIN; Cantonese voices known, only new recordings held out. No human speaker claim.'}
    (T/'training-independence.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps({k:v for k,v in result.items() if k!='training_checks'}))
if __name__=='__main__': main()
