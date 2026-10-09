"""Local ASR and signal diagnostics for a verified recovered upload; no network."""
import argparse
import csv
import json
from pathlib import Path

import numpy as np
from scipy import signal
import sherpa_onnx

from analyze_latency import load, correlate, sha


def metrics(pcm):
    frames = pcm[:len(pcm) // 320 * 320].reshape(-1, 320)
    levels = np.mean(np.abs(frames), axis=1) * 32768
    frequency, power = signal.welch(pcm, 16000, nperseg=min(4096, len(pcm)))
    peaks, _ = signal.find_peaks(power, distance=12)
    peaks = sorted(peaks, key=lambda i: power[i], reverse=True)[:8]
    total = float(power.sum())
    return dict(rms_dbfs=float(20 * np.log10(max(np.sqrt(np.mean(pcm**2)), 1e-12))),
                mean_abs_quantiles=np.percentile(levels, [10, 50, 90, 99]).tolist(),
                above_240_fraction=float(np.mean(levels > 240)),
                peaks_hz=[float(frequency[i]) for i in peaks],
                bands=[dict(hz=[low, high], power_fraction=float(power[(frequency >= low) & (frequency < high)].sum()) / total)
                       for low, high in [(0, 180), (180, 500), (500, 1500), (1500, 3500), (3500, 8001)]]
                if total else [])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('group', type=Path)
    parser.add_argument('--output-name', default='signal-review.json')
    args = parser.parse_args()
    directory = args.group / 'recovered-upload'
    if Path(args.output_name).name != args.output_name:
        parser.error('--output-name must be a local filename')
    output = directory / args.output_name
    if output.exists():
        raise FileExistsError(output)
    recovery = json.loads((directory / 'recovery.json').read_text(encoding='utf8'))
    if not recovery['crc_matches'] or not recovery['amplitude_matches']:
        raise ValueError('Recovered upload must be verified first')
    raw_path = directory / 'uploaded-prefix.wav'
    if sha(raw_path) != recovery['wav_sha256']:
        raise ValueError('Recovered audio changed')
    raw = load(raw_path)
    clean = np.frombuffer((directory / 'clean.pcm').read_bytes(), dtype='<i2').astype(np.float64) / 32768
    if len(clean) != len(raw):
        raise ValueError('Filtered sample count mismatch')
    source = load(args.group / 'prompt.wav')
    external = load(args.group / 'speaker.wav')
    sos = signal.butter(4, [1000, 3500], fs=16000, btype='bandpass', output='sos')
    match = correlate(signal.sosfiltfilt(sos, source), signal.sosfiltfilt(sos, raw))
    at = int(np.argmax(match))
    aligned = float(match[at]) >= .65
    source_end_s = (at + len(source)) / 16000
    analysis = json.loads((args.group / 'offline-analysis.json').read_text(encoding='utf8'))
    external_fit = analysis['source_alignment']['copies'][-1]
    external_start = external_fit['source_start_recording_s'] - at / 16000
    tail = slice(6 * 16000, 9 * 16000)
    external_interval = None
    if external_fit['accepted']:
        # If the board microphone cannot be waveform-aligned, retain that
        # failure and use a separately labelled external post-prompt window.
        begin = external_start + 6 if aligned and source_end_s < 6 else external_fit['source_start_recording_s'] + len(source) / 16000 + 2
        external_interval = [begin, begin + 3]
    root = Path(__file__).resolve().parents[2]
    model = root / 'artifacts/kws-phase2/asr'
    recognizer = sherpa_onnx.OfflineRecognizer.from_sense_voice(
        model=str(model / 'model.int8.onnx'), tokens=str(model / 'tokens.txt'),
        num_threads=4, use_itn=True, language='auto')
    segments = dict(raw_full=raw, clean_full=clean, raw_tail=raw[tail], clean_tail=clean[tail])
    if external_interval:
        name = 'external_aligned_tail' if aligned and source_end_s < 6 else 'external_post_prompt'
        start = round(external_interval[0] * 16000)
        segment = external[start:start + 48000]
        if len(segment) == 48000:
            segments[name] = segment
    results = {}
    for name, pcm in segments.items():
        stream = recognizer.create_stream()
        stream.accept_waveform(16000, pcm.astype(np.float32))
        recognizer.decode_stream(stream)
        results[name] = dict(text=stream.result.text.strip(), **metrics(pcm))
    with (directory / 'energy.csv').open(newline='') as f:
        rows = list(csv.DictReader(f))
    energy = {name: [int(r[name]) for r in rows] for name in rows[0]}
    result = dict(firmware=recovery['firmware'], round=recovery['round'],
                  wav_sha256=sha(raw_path), clean_sha256=sha(directory / 'clean.pcm'),
                  script_sha256=sha(Path(__file__)), model_sha256=sha(model / 'model.int8.onnx'),
                  tokens_sha256=sha(model / 'tokens.txt'), report_sha256=sha(args.group / 'report.json'),
                  energy_csv_sha256=sha(directory / 'energy.csv'),
                  match=dict(accepted=bool(aligned), correlation=float(match[at]), source_start_s=at / 16000,
                             source_end_s=source_end_s, external_capture_start_s=external_start),
                  tail_interval_s=[6, 9], external_interval_s=external_interval, segments=results,
                  c_energy_per_second=[dict(second=i, **{k: float(np.median([v for end, v in zip(energy['end_ms'], values) if i * 1000 < end <= (i + 1) * 1000]))
                                                       for k, values in energy.items() if k != 'end_ms'}) for i in range(9)],
                  limitation='ASR is automatic, not human listening. C filtering is exact, but no vendor VAD runs on the host. Different microphones have different gains; raw amplitude does not establish the physical noise source.')
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
