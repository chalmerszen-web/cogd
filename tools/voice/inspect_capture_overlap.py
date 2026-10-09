"""Offline capture diagnosis; prefix alignment never replaces latency acceptance."""
import argparse
import json
from pathlib import Path

import numpy as np
from scipy import signal
import sherpa_onnx
import soundfile as sf

from analyze_continuous import align_repeated
from analyze_latency import load, envelope, sha


def sweep(recording, low, high):
    # Same fixed180ms 2600->900Hz cue and .75 spectral fraction as the existing
    # feedback analyzer. Scan the input window, without USB receipt timestamps.
    f, t, power = signal.spectrogram(recording, fs=16000, nperseg=512,
                                    noverlap=432, mode='magnitude')
    band = (f >= 700) & (f <= 3000)
    peaks = f[band][np.argmax(power[band], axis=0)]
    candidates = []
    for start in np.arange(max(0, low), min(len(recording)/16000-.18, high), .005):
        age = t-start
        valid = (age >= .02) & (age <= .14)
        expected = 2600-1700*age/.18
        score = float(np.mean(abs(peaks[valid]-expected[valid]) < 150)) if valid.sum() >= 20 else 0
        candidates.append((score, float(start)))
    score, start = max(candidates)
    alternative = max((s for s, x in candidates if abs(x-start) > .25), default=0)
    return dict(present=score >= .75 and alternative < .75, score=score,
                alternative_score=alternative, start_s=start, end_s=start+.18)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    args = p.parse_args()
    out = args.directory/'capture-inspection'
    out.mkdir(exist_ok=False)
    report = json.loads((args.directory/'report.json').read_text(encoding='utf8'))
    assert report.get('ended')
    source, recording = [load(args.directory/n) for n in ('prompt.wav', 'speaker.wav')]
    assert sha(args.directory/'speaker.wav') == report['external_recording']['sha256']
    starts = [t['attempts'][0]['prompt_playback']['source_started'] for t in report['trials']]
    # Fixed before analysis: match the first2s, not an adaptively chosen crop.
    fit = align_repeated(source[:32000], recording, starts)
    model = Path('artifacts/kws-phase2/asr')
    recognizer = sherpa_onnx.OfflineRecognizer.from_sense_voice(
        model=str(model/'model.int8.onnx'), tokens=str(model/'tokens.txt'),
        num_threads=4, use_itn=True, language='auto')
    result = dict(accepted=False, latency_acceptance=False, prefix_seconds=2,
        script_sha256=sha(Path(__file__)), report_sha256=sha(args.directory/'report.json'),
        recording_sha256=sha(args.directory/'speaker.wav'),
        model_sha256=sha(model/'model.int8.onnx'), tokens_sha256=sha(model/'tokens.txt'),
        prefix_alignment=fit, rows=[],
        limitations=['Diagnostic only; full-source alignment failures remain unchanged.',
                     'Local ASR is not human listening; spectral cue timing is approximate.',
                     'A cue before source end indicates premature completion, not the exact VAD failure cause.'])
    energy = envelope(source)
    active = np.flatnonzero(energy >= max(64/32768, .02*energy.max()))
    active_end = (int(active[-1])+1)*.01
    for trial, match in zip(report['trials'], fit['copies']):
        row = dict(round=trial['round'], input_complete=trial['input_complete'])
        result['rows'].append(row)
        if not fit['accepted'] or not match['accepted']:
            row['unknown'] = 'Prefix does not meet unchanged alignment gates'
            continue
        start = match['source_start_recording_s']
        end = start+active_end
        row.update(source_active_end_s=end, cue=sweep(recording, start+.5, end+1.5))
        cue = row['cue']
        row['cue_before_source_end_s'] = end-cue['start_s'] if cue['present'] else None
        # Preserve a broad input window; content check is not phoneme alignment.
        a, b = max(0,start-.1), min(len(recording)/16000,start+len(source)/16000+.2)
        pcm = recording[round(a*16000):round(b*16000)].astype(np.float32)
        path = out/f'input-{trial["round"]}.wav'
        sf.write(path, pcm, 16000, subtype='PCM_16')
        stream = recognizer.create_stream();stream.accept_waveform(16000, pcm)
        recognizer.decode_stream(stream)
        row.update(offline_asr=stream.result.text, crop_sha256=sha(path), crop_s=[a,b])
    clip = args.directory/'last-device-clip/device-clip.wav'
    if clip.exists():
        metadata = json.loads((clip.parent/'report.json').read_text(encoding='utf8'))
        assert metadata['complete'] and sha(clip) == metadata['wav_sha256']
        pcm = load(clip).astype(np.float32)
        assert len(pcm) == metadata['samples']
        stream = recognizer.create_stream();stream.accept_waveform(16000, pcm)
        recognizer.decode_stream(stream)
        result['last_device_clip'] = dict(samples=len(pcm), sha256=sha(clip),
                                          offline_asr=stream.result.text)
    (out/'report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(dict(prefix_accepted=fit['accepted'],rows=result['rows'],
        last_device_clip=result.get('last_device_clip')),ensure_ascii=False))


if __name__ == '__main__':
    main()
