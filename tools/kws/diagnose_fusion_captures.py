"""Read-only signal/gain diagnosis of ten unblinded TEST captures, never training."""
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from device_domain import align, align_waveform, pcm
from evaluate import events
from parity import Backend


def score(signal, library):
    first = Backend(library, 'kws_trained_model')
    second = Backend(library, 'kws_secondary_model')
    values = []
    for frame in np.pad(signal, (0, (-len(signal)) % 256)).reshape(-1, 256):
        a, b = first.pcm(frame), second.pcm(frame)
        values.append(int((int(a.layer[264]) + int(b.layer[264])) / 2))
    scores = np.array(values, dtype=np.int16)
    blocks = scores[1::2].astype(float)
    for i in range(len(blocks)):
        scores[2 * i + 1] = int(blocks[max(0, i - 2):i + 1].mean())
    return scores


def main():
    base = ROOT / 'artifacts/kws-phase5/fusion-ef-smooth3'
    source_report = base / 'diagnostic-captures/report.json'
    report = json.loads(source_report.read_text(encoding='utf8'))
    assert report['complete'] and report['diagnostic_only'] and len(report['trials']) == 10
    library = ROOT / 'build-kws-fusion-host/libkws.so'
    parity = json.loads((base / 'fusion-host-parity.json').read_text())
    assert hashlib.sha256(library.read_bytes()).hexdigest() == parity['library_sha256']
    threshold = json.loads((base / 'threshold.json').read_text())['quantized']
    results = []
    warmup_samples = 32768
    for trial in report['trials']:
        row = trial['source']
        assert row['split'] == 'test'
        signal = pcm(ROOT / trial['path'])
        original = pcm(ROOT / row['path'])
        assert hashlib.sha256(signal.tobytes()).hexdigest() == trial['signal']['sha256']
        expected = trial['ready']['capture_samples'] + 16000 * (trial['audio']['source_started'] - trial['ready_query_finished'])
        result = dict(clip_id=row['clip_id'], language=row['language'], text=row['text'],
                      label=row['label'], playback_gain=row['playback_gain'],
                      adc_clipped=trial['state']['record_adc_clipped'], gains={})
        try:
            try:
                lag, correlation = align(original, signal, expected)
                alignment = dict(method='bounded_rms_envelope', correlation=correlation)
            except ValueError:
                lag, alignment = align_waveform(original, signal, expected)
            low = lag + row['speech_end_low_sample'] - 320
            high = lag + row['speech_end_high_sample'] + 320
            start = lag + row['speech_start_sample']
            speech = signal[max(0, start):min(len(signal), high)].astype(float)
            noise = signal[:4096].astype(float)
            rms = lambda x: float(np.sqrt(np.mean(x * x)))
            result.update(aligned=True, lag_samples=lag, alignment=alignment,
                          noise_rms=rms(noise), speech_plus_noise_rms=rms(speech),
                          power_contrast_db=float(20 * np.log10(max(rms(speech), 1) / max(rms(noise), 1))))
        except (ValueError, KeyError) as error:
            result.update(aligned=False, alignment_error=str(error))
            low = high = None
        padded = np.concatenate([np.resize(signal[:4096], warmup_samples), signal])
        for gain in (1, 2, 4):
            raw = padded.astype(np.int32) * gain
            amplified = np.clip(raw, -32768, 32767).astype(np.int16)
            scores = score(amplified, library)
            detected = [x - warmup_samples for x in events(scores, threshold) if x >= warmup_samples]
            result['gains'][str(gain)] = dict(events_in_capture_samples=detected,
                valid_hit=bool(row['label'] and low is not None and any(low <= x <= high + 12800 for x in detected)),
                peak_score_q8=int(scores.max()), clipped_samples=int(((raw < -32768) | (raw > 32767)).sum()))
        results.append(result)
    output = dict(diagnostic_only=True, threshold_q8=threshold, trials=results,
                  report_sha256=hashlib.sha256(source_report.read_bytes()).hexdigest(),
                  library_sha256=parity['library_sha256'],
                  warmup='Repeat first256ms of each recording as2.048s ambient prefill; this is offline reconstruction, not the original live listener.',
                  limitation='Power contrast includes noise, assumes locally stationary ambient, and is not calibrated SNR/SPL/distance. Gain grid is diagnostic; no operating point selected or firmware changed.')
    with (base / 'capture-diagnosis.json').open('x', encoding='utf8') as f:
        json.dump(output, f, ensure_ascii=False, indent=2)
    print(json.dumps(results, ensure_ascii=False))


if __name__ == '__main__':
    main()
