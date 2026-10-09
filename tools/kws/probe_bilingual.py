"""Frozen C11-model evaluation of fresh local SAPI sources; no training/hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools/kws'), str(ROOT / 'training/kws')]
from append_captures import capture_path
from device_domain import pcm
from diagnose_fusion_captures import score
from evaluate import events, threshold_q8


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, default=ROOT/'artifacts/kws-bilingual/sapi-probe')
    p.add_argument('--library', type=Path, default=ROOT/'build-kws-fusion-ek-host/libkws.so')
    p.add_argument('--threshold', type=int, default=740)
    p.add_argument('--name', default='baseline')
    a = p.parse_args()
    output = a.source/(a.name+'.json')
    if output.exists():
        raise ValueError('Preserve previous evidence')
    rows = json.loads((a.source/'manifest.json').read_text(encoding='utf8'))
    results = []
    seen = {}
    for row in rows:
        path = capture_path(row['path'])
        assert hashlib.sha256(path.read_bytes()).hexdigest() == row['wav_sha256']
        signal = pcm(path)
        energy = np.sqrt(np.mean(np.pad(signal.astype(float), (0, (-len(signal)) % 160)).reshape(-1, 160)**2, axis=1))
        active = np.flatnonzero(energy > max(50, energy.max()*.02))
        start, end = int(active[0])*160, (int(active[-1])+1)*160
        prepared = np.pad(signal, (32768, 16000))
        values = score(prepared, a.library)
        detected = [v-32768 for v in events(values, threshold_q8(a.threshold)) if v >= 32768]
        # Energy is not a phonetic endpoint; report all triggers and conservative
        # post-endpoint subset separately instead of relabeling an early event.
        duplicate = seen.get(row['wav_sha256'])
        seen.setdefault(row['wav_sha256'], row['clip_id'])
        results.append(dict(**row, duplicate_of=duplicate, samples=len(signal), speech_span_ms=(end-start)/16,
            energy_start=start, energy_end=end, events=detected, triggered=bool(detected),
            post_energy_hit=bool(row['label'] and any(end-320<=v<=end+12800 for v in detected)),
            peak_q8=int(values[129::2].max()), receptive_field_exceeded=end-start>32768))
    groups = {}
    for voice in sorted({r['source_group'] for r in results}):
        selected = [r for r in results if r['source_group']==voice and not r['duplicate_of']]
        groups[voice] = dict(positives=sum(r['label'] for r in selected),
            triggered_positives=sum(r['label'] and r['triggered'] for r in selected),
            post_energy_hits=sum(r['post_energy_hit'] for r in selected),
            negatives=sum(not r['label'] for r in selected),
            triggered_negatives=sum(not r['label'] and r['triggered'] for r in selected))
    report = dict(complete=True, groups=groups, trials=results, threshold_per_mille=a.threshold,
        threshold_q8=threshold_q8(a.threshold), library_sha256=hashlib.sha256(a.library.read_bytes()).hexdigest(),
        manifest_sha256=hashlib.sha256((a.source/'manifest.json').read_bytes()).hexdigest(),
        unique_wavs=len(seen), duplicate_rows=sum(bool(r['duplicate_of']) for r in results),
        limitation='Unseen local synthetic voices; digital input with silence prefill, not measured device acoustics or human recall. Energy endpoint is only a timing diagnostic.',
        training=False, threshold_retuned=False, hardware=False)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    print(json.dumps(groups, ensure_ascii=False))


if __name__ == '__main__':
    main()
