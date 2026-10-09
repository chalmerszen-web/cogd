"""Append bounded TRAIN-only fixed-noise mixtures, preserving all old features."""
import hashlib
import json
from pathlib import Path
import shutil
import numpy as np

from data import ROOT, Frontend, framed_example, normalize, read_manifest, load_pcm
from append_captures import capture_path
from device_domain import pcm
from paired_negative import incomplete


def mix_weak(signal, ambient, db):
    if db not in (6, 12):
        raise ValueError('Unregistered attenuation')
    if not len(ambient):
        raise ValueError('Missing TRAIN ambient')
    scale = 10 ** (-db / 20)
    mixed = np.asarray(signal, dtype=float) * scale
    mixed += np.resize(ambient, len(mixed)).astype(float) * np.sqrt(1 - scale * scale)
    clipped = int(np.count_nonzero(np.abs(mixed) > 32767))
    return np.clip(np.rint(mixed), -32768, 32767).astype(np.int16), clipped


def measured_sources(base, parents):
    result = []
    original = json.loads((base/'features/features.json').read_text(encoding='utf8'))
    for cap in original['captures']:
        if cap['split'] != 'train':
            continue
        parent = parents[cap['parent_clip_id']]
        recorded = pcm(capture_path(cap['recording']))
        assert hashlib.sha256(recorded.tobytes()).hexdigest() == cap['pcm_sha256']
        lo, hi = cap['crop']
        row = dict(parent, clip_id=cap['clip_id'],
                   speech_start_sample=max(0, cap['lag_samples']-lo+parent['speech_start_sample']),
                   speech_end_low_sample=cap['end_interval'][0],
                   speech_end_high_sample=cap['end_interval'][1])
        result.append((row, recorded[lo:hi], recorded[-16000:], cap['recording']))
    assert len(result) == 44
    for corpus, audit_name, expected in (('corpus-full-phrase','full-phrase-capture-audit.json',41),
                                         ('corpus-word-contrast','word-contrast-audit.json',28)):
        report_path = base/corpus/'report.json'
        audit = json.loads((base/audit_name).read_text(encoding='utf8'))
        assert hashlib.sha256(report_path.read_bytes()).hexdigest() == audit['source_report_sha256']
        trials = {t['source']['clip_id']:t for t in json.loads(report_path.read_text(encoding='utf8'))['trials']}
        eligible = [c for c in audit['trials'] if c['eligible']]
        assert len(eligible) == expected
        for cap in eligible:
            parent = parents[cap['parent_clip_id']]
            trial = trials[cap['clip_id']]
            recorded = pcm(capture_path(trial['path']))
            assert hashlib.sha256(recorded.tobytes()).hexdigest() == trial['signal']['sha256']
            source = load_pcm(parent)
            lag = cap['lag_samples']
            lo, hi = max(0, lag-1600), min(len(recorded), lag+len(source)+1600)
            row = dict(parent, clip_id='device-extra-'+cap['clip_id'],
                       speech_start_sample=lag-lo+parent['speech_start_sample'],
                       speech_end_low_sample=lag-lo+parent['speech_end_low_sample']-320,
                       speech_end_high_sample=lag-lo+parent['speech_end_high_sample']+320)
            result.append((row, recorded[lo:hi], recorded[-16000:], trial['path']))
    assert len(result) == 113
    return result


def main():
    base = ROOT/'artifacts/kws-phase5'
    original_dir = base/'features-word-contrast'
    out = base/'features-weak-logmel'
    out.mkdir(exist_ok=False)
    rows = read_manifest(ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl')
    parents = {r['clip_id']:r for r in rows}
    forbidden = {r['source_group'] for r in rows if r['split'] != 'train'}
    measured = measured_sources(base, parents)
    assert all(r['split']=='train' and r['source_group'] not in forbidden for r,_,_,_ in measured)
    original = np.load(original_dir/'train.npz', allow_pickle=False)
    stats = json.loads((original_dir/'normalization.json').read_text())
    frontend = Frontend()
    rng = np.random.default_rng(20261004)
    examples, ancestry = [], []
    clipped = 0
    for index, (row, source, noise, recording) in enumerate(measured):
        variants = [(row, source)]
        if row['label']:
            for side in ('prefix','suffix'):
                part,_ = incomplete(source,row['speech_start_sample'],row['speech_end_low_sample'],side,ambient=noise)
                variants.append((dict(row,label=0,clip_id=row['clip_id']+'-'+side,
                                     text='time-cut-'+side,sampling_group='paired_negative'),part))
        for variant_row, signal in variants:
            framed,labels,end = framed_example(variant_row, signal)
            offset = (max(4096,33792-variant_row['speech_end_low_sample'])+len(framed)-len(signal)-1024)//2
            floor = np.resize(noise,len(framed))
            framed[:offset] = floor[:offset]
            framed[offset+len(signal):] = floor[offset+len(signal):]
            for db in (6,12):
                choices = [i for i in range(len(measured)) if i != index]
                other = int(rng.choice(choices))
                background = np.roll(measured[other][2],int(rng.integers(16000)))
                wave, nclip = mix_weak(framed, background, db)
                clipped += nclip
                examples.append((variant_row, normalize(frontend(wave),stats), labels, end, db))
                ancestry.append(dict(clip_id=variant_row['clip_id'],recording=recording,
                    source_group=row['source_group'],split='train',attenuation_db=db,
                    noise_clip_id=measured[other][0]['clip_id'],noise_split='train',
                    noise_recording=measured[other][3],pcm_sha256=hashlib.sha256(wave.tobytes()).hexdigest(),
                    clipped_samples=nclip))
        if index%20 == 0:
            print(f'Weak measured TRAIN sources:{index+1}/{len(measured)}',flush=True)
    extra = dict(x=np.stack([e[1] for e in examples]),y=np.stack([e[2] for e in examples]),
        label=np.array([e[0]['label'] for e in examples]),language=np.array([e[0]['language'] for e in examples]),
        clip_id=np.array(['weak-logmel-'+e[0]['clip_id'] for e in examples]),
        source_group=np.array([e[0]['source_group'] for e in examples]),
        event_end=np.array([e[3] if e[3] is not None else -1 for e in examples]),
        event_start_accept=np.array([e[3]-(e[0]['speech_end_high_sample']-e[0]['speech_end_low_sample'])
                                    if e[3] is not None else -1 for e in examples]),
        sampling_group=np.array([e[0].get('sampling_group','positive' if e[0]['label'] else 'hard_negative') for e in examples]),
        text=np.array([e[0].get('text','') for e in examples]),variant=np.array([e[4] for e in examples]),
        device_domain=np.ones(len(examples),dtype=bool))
    assert set(extra) == set(original.files)
    assert not set(extra['clip_id'])&set(original['clip_id'])
    assert not set(extra['source_group'])&forbidden
    np.savez_compressed(out/'train.npz', **{k:np.concatenate((original[k],extra[k])) for k in extra})
    saved = np.load(out/'train.npz',allow_pickle=False)
    assert all(np.array_equal(saved[k][:len(original[k])],original[k]) for k in original.files)
    for name in ('validation.npz','test.npz','normalization.json'):
        shutil.copyfile(original_dir/name,out/name)
        assert (out/name).read_bytes() == (original_dir/name).read_bytes()
    metadata = dict(complete=True,seed=20261004,captures=len(measured),added=len(examples),
        old_examples=len(original['x']),total_examples=len(saved['x']),exact_original_prefix=True,
        split_sha256={s:hashlib.sha256((out/(s+'.npz')).read_bytes()).hexdigest() for s in ('train','validation','test')},
        original_train_sha256=hashlib.sha256((original_dir/'train.npz').read_bytes()).hexdigest(),
        frontend='unchanged_logmel',frontend_library_sha256=hashlib.sha256((ROOT/'build-kws-host/libkws.so').read_bytes()).hexdigest(),
        normalization_unchanged_sha256=hashlib.sha256((out/'normalization.json').read_bytes()).hexdigest(),
        clipped_samples=clipped,ancestry=ancestry,test_waveforms_read=False,
        limitation='Approximate TRAIN-only weak mixtures; no new voice or physical-distance evidence.')
    (out/'features.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({k:v for k,v in metadata.items() if k!='ancestry'}),flush=True)


if __name__=='__main__':main()
