"""Finite PCEN corpus from existing source-separated development WAVs only."""
import hashlib
import json
from pathlib import Path
import numpy as np
from data import ROOT, read_manifest, load_pcm, framed_example, normalization, normalize
from device_domain import pcm
from append_captures import capture_path
from paired_negative import incomplete
from pcen_frontend import PcenFrontend

OUT = ROOT / 'artifacts/kws-phase5/pcen-g-features'


def main():
    OUT.mkdir(exist_ok=False)
    base = ROOT / 'artifacts/kws-phase5'
    rows = read_manifest(ROOT / 'artifacts/kws-phase3/dataset/manifest.jsonl')
    parents = {r['clip_id']: r for r in rows}
    forbidden = {r['source_group'] for r in rows if r['split'] == 'test'}
    captures = json.loads((base / 'features/features.json').read_text(encoding='utf8'))['captures']
    channel = json.loads((base / 'features-channel/features.json').read_text())
    noise = [pcm(capture_path(c['recording']))[-16000:] for c in captures if c['split'] == 'train']
    assert len(noise) == 44
    frequency = np.fft.rfftfreq(512, 1 / 16000)
    magnitude = 10 ** (np.interp(frequency, channel['centre_frequencies'], channel['median_response_db']) / 20)
    impulse = np.roll(np.fft.irfft(magnitude, n=512), 256) * np.hanning(512)
    frontend = PcenFrontend()
    rng = np.random.default_rng(20260930)
    examples = dict(train=[], validation=[], weak6=[], weak12=[])
    ancestry = []

    def add(row, signal, ambient, device, variant=0, weak_db=0):
        split = row['split']
        assert split in ('train', 'validation') and row['source_group'] not in forbidden
        framed, labels, end = framed_example(row, signal, rng if variant and not device else None)
        if device:
            offset = (max(4096, 33792 - row['speech_end_low_sample']) + len(framed) - len(signal) - 1024) // 2
            floor = np.resize(ambient, len(framed))
            framed[:offset] = floor[:offset]
            framed[offset + len(signal):] = floor[offset + len(signal):]
            if weak_db:
                scale = 10 ** (-weak_db / 20)
                independent = np.resize(np.roll(ambient, 7919), len(framed))
                framed = np.clip(np.rint(framed.astype(float) * scale + independent * np.sqrt(1 - scale * scale)), -32768, 32767).astype(np.int16)
        elif variant:
            # Same TRAIN-only measured channel family; explicitly low SNR.
            colored = np.convolve(framed.astype(float), impulse, mode='same')
            floor = np.resize(noise[int(rng.integers(len(noise)))], len(framed)).astype(float)
            active = np.abs(colored) > max(1., np.max(np.abs(colored)) * .08)
            speech_rms = np.sqrt(np.mean(colored[active] ** 2)) if active.any() else 1.
            snr = rng.uniform(-6, 18)
            colored *= max(1., np.sqrt(np.mean(floor ** 2))) * 10 ** (snr / 20) / max(1., speech_rms)
            framed = np.clip(np.rint(colored + floor), -32768, 32767).astype(np.int16)
        target = 'weak' + str(weak_db) if split == 'validation' and weak_db else split
        feature = frontend(framed)
        examples[target].append((row, feature, labels, end, device, variant))
        assert sum(map(len, examples.values())) <= 16000

    def source_variants(row, signal, ambient=None, device=False):
        variants = [(row, signal)]
        if row['label']:
            for side in ('prefix', 'suffix'):
                part, _ = incomplete(signal, row['speech_start_sample'], row['speech_end_low_sample'], side, ambient=ambient)
                variants.append((dict(row, label=0, clip_id=row['clip_id'] + '-' + side,
                                      text='time-cut-' + side, sampling_group='paired_negative'), part))
        for derived, wave in variants:
            if device:
                levels = (0, 6, 12, 18) if row['split'] == 'train' else (0, 6, 12)
                for index, db in enumerate(levels):
                    add(derived, wave, ambient, True, index, db)
            else:
                for variant in range(3 if row['split'] == 'train' else 1):
                    add(derived, wave, None, False, variant)
        if device:
            quiet = dict(row, label=0, clip_id=row['clip_id'] + '-ambient', text='', sampling_group='natural_negative')
            # Preserve exactly the same ambient-negative construction as the old validation.
            signal = np.resize(ambient, 65536).astype(np.int16)
            for db in ((0,) if row['split'] == 'train' else (0, 6, 12)):
                x = signal if not db else np.clip(np.rint(signal * 10 ** (-db / 20) + np.resize(np.roll(ambient, 7919), 65536) * np.sqrt(1 - 10 ** (-db / 10))), -32768, 32767).astype(np.int16)
                target = 'weak' + str(db) if db else row['split']
                examples[target].append((quiet, frontend(x), np.zeros(256, np.float32), None, True, db))

    for index, row in enumerate(r for r in rows if r['split'] in ('train', 'validation')):
        signal = load_pcm(row)
        source_variants(row, signal)
        ancestry.append(dict(clip_id=row['clip_id'], parent_clip_id=row['original_recording_id'],
                             split=row['split'], source_group=row['source_group'], pcm_sha256=row['pcm_sha256']))
        if index % 200 == 0:
            print(f'Original development sources processed:{index + 1}', flush=True)

    for cap in captures:
        parent = parents[cap['parent_clip_id']]
        recorded = pcm(capture_path(cap['recording']))
        lo, hi = cap['crop']
        row = dict(parent, clip_id=cap['clip_id'], speech_start_sample=max(0, cap['lag_samples'] - lo + parent['speech_start_sample']),
                   speech_end_low_sample=cap['end_interval'][0], speech_end_high_sample=cap['end_interval'][1])
        source_variants(row, recorded[lo:hi], recorded[-16000:], True)
        ancestry.append(dict(clip_id=row['clip_id'], parent_clip_id=parent['clip_id'], split=row['split'],
                             source_group=row['source_group'], recording=cap['recording']))

    audit = json.loads((base / 'full-phrase-capture-audit.json').read_text(encoding='utf8'))
    corpus_path = base / 'corpus-full-phrase/report.json'
    assert hashlib.sha256(corpus_path.read_bytes()).hexdigest() == audit['source_report_sha256']
    trials = {t['source']['clip_id']: t for t in json.loads(corpus_path.read_text(encoding='utf8'))['trials']}
    accepted = [r for r in audit['trials'] if r['eligible']]
    assert len(accepted) == 41
    for cap in accepted:
        parent = parents[cap['parent_clip_id']]
        assert parent['split'] == 'train'
        trial = trials[cap['clip_id']]
        recorded = pcm(capture_path(trial['path']))
        assert hashlib.sha256(recorded.tobytes()).hexdigest() == trial['signal']['sha256']
        source = load_pcm(parent)
        lag = cap['lag_samples']
        lo, hi = max(0, lag - 1600), min(len(recorded), lag + len(source) + 1600)
        row = dict(parent, clip_id='device-extra-' + cap['clip_id'],
                   speech_start_sample=lag - lo + parent['speech_start_sample'],
                   speech_end_low_sample=lag - lo + parent['speech_end_low_sample'] - 320,
                   speech_end_high_sample=lag - lo + parent['speech_end_high_sample'] + 320)
        source_variants(row, recorded[lo:hi], recorded[-16000:], True)
        ancestry.append(dict(clip_id=row['clip_id'], parent_clip_id=parent['clip_id'], split='train',
                             source_group=row['source_group'], recording=trial['path']))

    stats = normalization([e[1] for e in examples['train']])
    stats['frontend'] = 'pcen_v1'
    (OUT / 'normalization.json').write_text(json.dumps(stats, indent=2))
    for split, selected in examples.items():
        np.savez_compressed(OUT / (split + '.npz'),
            x=np.stack([normalize(e[1], stats) for e in selected]), y=np.stack([e[2] for e in selected]),
            language=np.array([e[0]['language'] for e in selected]), label=np.array([e[0]['label'] for e in selected]),
            clip_id=np.array([e[0]['clip_id'] for e in selected]), source_group=np.array([e[0]['source_group'] for e in selected]),
            event_end=np.array([e[3] if e[3] is not None else -1 for e in selected]),
            event_start_accept=np.array([e[3] - (e[0].get('speech_end_high_sample', 0) - e[0].get('speech_end_low_sample', 0)) if e[3] is not None else -1 for e in selected]),
            sampling_group=np.array([e[0].get('sampling_group', 'positive' if e[0]['label'] else 'natural_negative') for e in selected]),
            text=np.array([e[0].get('text', '') for e in selected]), variant=np.array([e[5] for e in selected]),
            device_domain=np.array([e[4] for e in selected]))
    report = dict(complete=True, seed=20260930, counts={k: len(v) for k, v in examples.items()},
                  frontend='pcen_v1', frontend_library_sha256=hashlib.sha256((ROOT / 'build-kws-pcen-prototype/libkws.so').read_bytes()).hexdigest(),
                  ancestry=ancestry, normalization_split='train', forbidden_test_groups=sorted(forbidden),
                  test_waveforms_read=False, diagnostic_recordings_used=False,
                  limitation='Joint frontend/data experiment; not a controlled frontend-only attribution. Weak mixtures are approximate, not physical metres.')
    (OUT / 'features.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps(report['counts']), flush=True)


if __name__ == '__main__':
    main()
