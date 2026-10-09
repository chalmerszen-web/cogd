"""One frozen frontend recipe over exact captured validation and fixed weak mixtures."""
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from data import Frontend, framed_example, normalize
from device_domain import pcm
from paired_negative import incomplete
from evaluate import metrics
from quantize import infer
from noise_reduction_pilot import reduce
from test_frozen_fusion import combine


def main():
    base = ROOT / 'artifacts/kws-phase5'
    out = base / 'noise-frontend-pilot'
    out.mkdir(exist_ok=False)
    path = base / 'features-paired/validation.npz'
    data = np.load(path, allow_pickle=False)
    indices = np.flatnonzero(data['device_domain'])
    assert len(indices) == 88
    subset = {key: data[key][indices] for key in data.files if key != 'device_domain'}
    saved = np.load(base / 'fusion-ef-smooth3/validation-scores.npz')['quantized'][indices]
    stats = json.loads((base / 'features-paired/normalization.json').read_text())
    bundles = [json.loads((base / (name + '-int8/model.json')).read_text()) for name in ('adapt-e', 'adapt-f')]
    captures = json.loads((base / 'features/features.json').read_text(encoding='utf8'))['captures']
    captures = {r['clip_id']: r for r in captures if r['split'] == 'validation'}
    parents = {r['clip_id']: r for r in map(json.loads, (ROOT / 'artifacts/kws-phase3/dataset/manifest.jsonl').read_text(encoding='utf8').splitlines())}
    plan = dict(max_waveforms=264, attenuation_db=[0, 6, 12], threshold_q8=281,
                recipe='Magnitude EMA1/32 subtraction with50percent magnitude floor',
                validation_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                test_access=False, training=False, firmware_changes=False)
    (out / 'plan.json').write_text(json.dumps(plan, indent=2))
    frontend = Frontend()
    score_sets = {db: {kind: [] for kind in ('baseline', 'reduced')} for db in (0, 6, 12)}
    for position, i in enumerate(indices):
        clip = str(data['clip_id'][i])
        kind = str(data['sampling_group'][i])
        key = clip
        for suffix in ('-ambient', '-prefix', '-suffix'):
            if key.endswith(suffix):
                key = key[:-len(suffix)]
                break
        cap = captures[key]
        parent = parents[cap['parent_clip_id']]
        assert cap['split'] == parent['split'] == 'validation'
        recorded = pcm(ROOT / cap['recording'])
        noise = recorded[-16000:]
        lo, hi = cap['crop']
        source = recorded[lo:hi]
        row = dict(parent, label=int(data['label'][i]), speech_end_low_sample=cap['end_interval'][0],
                   speech_end_high_sample=cap['end_interval'][1])
        if kind == 'natural_negative':
            signal = np.resize(noise, 65536).copy()
        else:
            if kind == 'paired_negative':
                start = max(0, cap['lag_samples'] - lo + parent['speech_start_sample'])
                source, _ = incomplete(source, start, row['speech_end_low_sample'], clip.rsplit('-', 1)[-1], ambient=noise)
            signal, _, _ = framed_example(row, source)
            offset = (max(4096, 33792 - row['speech_end_low_sample']) + len(signal) - len(source) - 1024) // 2
            floor = np.resize(noise, len(signal))
            signal[:offset] = floor[:offset]
            signal[offset + len(source):] = floor[offset + len(source):]
        original = frontend(signal)
        assert np.array_equal(normalize(original, stats), data['x'][i]), clip
        ambient = np.resize(np.roll(noise, 7919), len(signal)).astype(float)
        for db in (0, 6, 12):
            scale = 10 ** (-db / 20)
            mixed = np.clip(np.rint(signal.astype(float) * scale + ambient * np.sqrt(1 - scale ** 2)), -32768, 32767).astype(np.int16)
            raw = original if db == 0 else frontend(mixed)
            for label, features in (('baseline', normalize(raw, stats)), ('reduced', normalize(reduce(raw)[0], stats))):
                members = [infer(features, bundle)[:, -1][None, :] for bundle in bundles]
                score = combine(*members, integer=True)[0]
                if db == 0 and label == 'baseline':
                    assert np.array_equal(score, saved[position]), clip
                score_sets[db][label].append(score)
        if position % 20 == 0:
            print(f'{position + 1}/88 captured validation examples', flush=True)
    results = {}
    for db, groups in score_sets.items():
        results[str(db)] = {}
        for kind, scores in groups.items():
            array = np.stack(scores)
            results[str(db)][kind] = metrics(array, subset, 281)
            np.savez_compressed(out / f'scores-{db}-{kind}.npz', scores=array, clip_id=subset['clip_id'])
    clean = results['0']['reduced']
    clean_ok = all(v['hits'] == 8 for v in clean['languages'].values()) and clean['negative_triggered_clips'] == 0
    nonregressing = all(results[str(db)]['reduced']['languages'][lang]['hits'] >= results[str(db)]['baseline']['languages'][lang]['hits']
                        for db in (6, 12) for lang in ('zh', 'yue'))
    negative_ok = all(results[str(db)]['reduced']['negative_triggered_clips'] <= results[str(db)]['baseline']['negative_triggered_clips'] for db in (6, 12))
    improvement = sum(results[str(db)]['reduced']['languages'][lang]['hits'] - results[str(db)]['baseline']['languages'][lang]['hits']
                      for db in (6, 12) for lang in ('zh', 'yue'))
    report = dict(complete=True, exact_original_features_and_scores=88, results=results,
                  gate=dict(clean=clean_ok, weak_nonregression=nonregressing, negative_nonregression=negative_ok, weak_extra_hits=improvement),
                  advance=clean_ok and nonregressing and negative_ok and improvement > 0,
                  limitation='Offline approximate noise mixtures of reused validation captures. Not independent trials, deployable C, physical weak replay or5m proof.')
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps({db: {kind: dict(languages={lang: v['hits'] for lang, v in r['languages'].items()}, negative=r['negative_triggered_clips']) for kind, r in variants.items()} for db, variants in results.items()}))
    print(json.dumps(report['gate']))


if __name__ == '__main__':
    main()
