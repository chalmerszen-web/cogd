"""Compare a retained last device clip with logged uploaded PCM; local ASR only."""
import argparse
import hashlib
import json
from pathlib import Path
import wave
import zlib

import numpy as np
import sherpa_onnx


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output-name', default='uploaded-clip-review.json')
    args = parser.parse_args()
    if Path(args.output_name).name != args.output_name:
        parser.error('--output-name must be a filename in the report directory')
    output = args.directory / args.output_name
    if output.exists():
        raise FileExistsError(output)
    report_path = args.directory / 'report.json'
    report = json.loads(report_path.read_text(encoding='utf8'))
    trial = report['trials'][-1]
    upload = json.loads(next(e['text'] for e in trial['events'] if e['stage'] == 'fast_upload_stats'))
    clip_path = args.directory / 'device-last-clip/device-clip.wav'
    with wave.open(str(clip_path), 'rb') as wav:
        if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (16000, 1, 2):
            raise ValueError('Expected 16 kHz mono PCM16')
        raw = wav.readframes(wav.getnframes())
    count = upload['samples']
    if count * 2 > len(raw):
        raise ValueError('Clip is shorter than the submitted PCM')
    prefix = raw[:count * 2]
    crc = f'{zlib.crc32(prefix):08x}'
    pcm = np.frombuffer(prefix, dtype='<i2').astype(np.int32)
    absolute = np.abs(pcm)
    measured = dict(peak=int(absolute.max()) if count else 0,
                    mean_abs=int(absolute.sum()) // count if count else 0,
                    zero_samples=int(np.count_nonzero(pcm == 0)))
    model = Path(__file__).resolve().parents[2] / 'artifacts/kws-phase2/asr'
    recognizer = sherpa_onnx.OfflineRecognizer.from_sense_voice(
        model=str(model / 'model.int8.onnx'), tokens=str(model / 'tokens.txt'),
        num_threads=4, use_itn=True, language='auto')
    transcripts = {}
    segments = [('submitted_prefix', prefix), ('completed_clip', raw)]
    boundaries = {}
    for event in trial['events']:
        if event['stage'] in ('cloud_speech_begin', 'cloud_speech_end'):
            boundaries.update(json.loads(event['text']))
    begin, end = boundaries.get('audio_start_ms'), boundaries.get('audio_end_ms')
    cloud_range = None
    if begin is not None and end is not None and 0 <= begin < end <= count / 16:
        cloud_range = [begin, end]
        segments.append(('cloud_endpoint_slice', prefix[begin * 32:end * 32]))
    for label, data in segments:
        stream = recognizer.create_stream()
        stream.accept_waveform(16000, np.frombuffer(data, dtype='<i2').astype(np.float32) / 32768)
        recognizer.decode_stream(stream)
        transcripts[label] = stream.result.text.strip()
    result = dict(firmware=report['before']['version'], round=trial['round'],
                  report_sha256=sha(report_path), clip_sha256=sha(clip_path),
                  script_sha256=sha(Path(__file__)), model_sha256=sha(model / 'model.int8.onnx'),
                  tokens_sha256=sha(model / 'tokens.txt'), submitted_samples=count,
                  clip_samples=len(raw) // 2, logged_crc32=upload['pcm_crc32'], clip_prefix_crc32=crc,
                  crc_matches=crc == upload['pcm_crc32'], measured_amplitude=measured,
                  amplitude_matches=all(upload.get(k) == v for k, v in measured.items()),
                  cloud_range_ms=cloud_range, offline_asr=transcripts, cloud_asr=trial.get('asr'),
                  limitation='CRC compares the stored clip with PCM submitted to the device WebSocket encoder. It does not prove cloud processing or human transcription accuracy.')
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
