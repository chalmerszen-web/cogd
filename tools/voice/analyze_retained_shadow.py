"""Local input diagnosis from retained clips and exact shadow endpoint replay.

This never opens USB, plays audio, trains a model or calls a cloud service.
Shadow-prefix transcripts are counterfactuals, not recovered earlier failures.
"""
import argparse
import json
from pathlib import Path

import numpy as np
import sherpa_onnx

from analyze_latency import align, envelope, load, sha
from input_integrity import complete_input

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    group = args.directory.resolve()
    output = group/'retained-shadow-analysis.json'
    if output.exists(): raise FileExistsError(output)
    report = json.loads((group/'report.json').read_text(encoding='utf8'))
    endpoints = json.loads((group/'endpoint-analysis/summary.json').read_text(encoding='utf8'))
    if len(report['trials']) != 3 or not endpoints['complete']:
        raise ValueError('Need three retained turns and complete exact endpoint replay')
    source = load(group/'prompt.wav')
    activity = np.flatnonzero(envelope(source) >= max(64/32768, envelope(source).max()*.02))
    if not len(activity): raise ValueError('No source activity')
    source_active_end = (int(activity[-1])+1)*.01
    model = ROOT/'artifacts/kws-phase2/asr'
    recognizer = sherpa_onnx.OfflineRecognizer.from_sense_voice(
        model=str(model/'model.int8.onnx'), tokens=str(model/'tokens.txt'),
        num_threads=4, use_itn=True, language='auto')

    def transcribe(pcm):
        stream = recognizer.create_stream()
        stream.accept_waveform(16000, pcm.astype(np.float32))
        recognizer.decode_stream(stream)
        text = stream.result.text.strip()
        return dict(text=text, complete_input=complete_input(report['prompt'], text))

    rows = []
    for trial in report['trials']:
        number = trial['round']
        clip_dir = group/f'trial-{number:02d}/retained-device-clip'
        clip_path = clip_dir/'device-clip.wav'
        retention = json.loads((clip_dir/'retention.json').read_text(encoding='utf8'))
        exported = json.loads((clip_dir/'report.json').read_text(encoding='utf8'))
        if not retention['complete'] or exported['wav_sha256'] != sha(clip_path):
            raise ValueError('Unverified retained capture')
        raw = load(clip_path)
        if exported['samples'] != len(raw) or len(raw) != 128000:
            raise ValueError('Unexpected diagnostic capture length')
        traces = [t for t in endpoints['traces'] if t['round'] == number]
        if len(traces) != 1 or not traces[0]['parity_passed'] or not traces[0]['header']['shadow']:
            raise ValueError('Missing exact shadow trace for this turn')
        header = traces[0]['header']
        terminal = header['elapsed_ms']/1000
        matched = align(source,raw)  # Existing fixed gates, never relaxed here.
        prefix = raw[:round(terminal*16000)]
        row = dict(round=number, language=trial['language'],
            capture_sha256=sha(clip_path), captured_samples=len(raw),
            source_match=matched, source_active_end_s=source_active_end,
            logical_shadow_terminal_s=terminal, frozen_noise=header['noise'],
            full_clip_asr=transcribe(raw), logical_shadow_prefix_asr=transcribe(prefix),
            device_asr=trial['asr'],
            premature_shadow_end=None, post_terminal_active_frames=None)
        frames = np.frombuffer((group/'endpoint-analysis'/f"trace-{header['id']:02d}.bin").read_bytes(),
                               dtype=np.dtype([('level','<u2'),('clean','<u2'),('spectral','u1'),('tag','u1')]))
        if len(frames) != 400: raise ValueError('Incomplete eight-second metadata')
        tail = frames[header['elapsed_ms']//20:]
        row['post_terminal_active_frames'] = int(np.count_nonzero(
            (tail['spectral'] != 0) & (tail['clean'] >= max(240,header['noise']*2))))
        if matched['accepted']:
            end = matched['offset_s']+source_active_end
            row.update(source_active_end_in_clip_s=end,
                       premature_shadow_end=terminal < end-.05)
        rows.append(row)
    result = dict(complete=True, local_only=True, trials=rows,
        report_sha256=sha(group/'report.json'), endpoint_summary_sha256=sha(group/'endpoint-analysis/summary.json'),
        source_sha256=sha(group/'prompt.wav'), script_sha256=sha(Path(__file__)),
        model_sha256=sha(model/'model.int8.onnx'), tokens_sha256=sha(model/'tokens.txt'),
        inference_count=6, response_latency_acceptance_eligible=False,
        limitations=['One known public fixture group; no independent human/generalization proof.',
            'Local ASR is automatic; unclear waveform alignment stays unknown.',
            'Logical shadow prefix is from the new eight-second clip, not recovered UX282 audio.',
            'Per-turn export and fixed long capture exclude speed and rapid-rearm acceptance.'])
    output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(dict(complete=True, trials=rows),ensure_ascii=False),flush=True)


if __name__ == '__main__': main()
