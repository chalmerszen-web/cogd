"""Offline UX63: one fixed C filter, three retained device PCM pairs; no network/USB."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import wave

import numpy as np
from scipy import signal
import sherpa_onnx

from input_integrity import complete_input

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'artifacts/voice-fast'
CASES = (
    ('greeting', 'endpoint-trace-01137-greeting-01', 'device-last-clip', (2.1, 2.6)),
    ('correction', 'endpoint-01162-correction-01', 'last-device-clip', (7, 9)),
    ('blue', 'capture-loss-01186-blue-01', 'last-device-clip', (4.1, 4.7)),
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf8'))


def wav_pcm(path):
    with wave.open(str(path), 'rb') as w:
        if (w.getframerate(), w.getnchannels(), w.getsampwidth()) != (16000, 1, 2):
            raise ValueError('Expected PCM16 mono 16 kHz: ' + str(path))
        return w.readframes(w.getnframes())


def write_wav(path, pcm):
    with wave.open(str(path), 'wb') as w:
        w.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
        w.writeframes(pcm)


def metrics(pcm):
    x = np.frombuffer(pcm, dtype='<i2').astype(np.float64)
    hz, power = signal.welch(x, 16000, nperseg=min(4096, len(x)))
    total = max(float(power.sum()), 1e-30)
    return dict(rms=float(np.sqrt(np.mean(x*x))), peak=float(np.max(np.abs(x))),
        full_scale_samples=int(np.count_nonzero((x == -32768) | (x == 32767))),
        spectral_fraction={f'{a}-{b}': float(power[(hz >= a) & (hz < b)].sum()) / total
            for a, b in ((0, 180), (180, 500), (500, 1500), (1500, 3500), (3500, 8001))})


def prepare(out, renderer):
    out = out.resolve()
    renderer = renderer.resolve()
    if not out.is_relative_to(ROOT) or not renderer.is_relative_to(ROOT):
        raise ValueError('Study files must be in the workspace')
    if not renderer.is_file():
        raise FileNotFoundError(renderer)
    out.mkdir(parents=True, exist_ok=False)
    (out / 'executed-prepare.py').write_bytes(Path(__file__).read_bytes())
    sources = {str(p.relative_to(ROOT)): sha(p.read_bytes()) for p in
        (Path(__file__), ROOT/'tools/voice/asr_filter_render.c', ROOT/'plugins/audio/tonal.c',
         ROOT/'plugins/audio/factor_q30.h', renderer)}
    model = ROOT/'artifacts/kws-phase2/asr'
    recognizer = sherpa_onnx.OfflineRecognizer.from_sense_voice(model=str(model/'model.int8.onnx'),
        tokens=str(model/'tokens.txt'), num_threads=4, use_itn=True, language='auto')
    result = dict(prepared_asr_inputs=1, filter='existing_tonal_highpass_300Hz_Q30',
        candidate_not_installed=True, cloud_prerequisites_pass=False, source_hashes=sources,
        local_asr_model_sha256=sha((model/'model.int8.onnx').read_bytes()), fixtures=[], pairs=[],
        limitations=['Three retained synthetic-source acoustic recordings; no new human or device test.',
            'Named analysis windows are fixed intervals, not proven clean silence.',
            '500 ms zero padding is declared; endpoint improvements in that padding do not count.'])
    root_posix = '/mnt/' + ROOT.drive[0].lower() + ROOT.as_posix()[2:]
    for label, group, clip_dir, interval in CASES:
        source_dir = BASE/group/clip_dir
        clip_report = read(source_dir/'report.json')
        source = source_dir/'device-clip.wav'
        pcm = wav_pcm(source)
        if not clip_report['complete'] or sha(pcm) != clip_report['pcm_sha256'] or len(pcm) != clip_report['samples']*2:
            raise ValueError('Retained device PCM failed verification: ' + label)
        original_samples = len(pcm)//2
        padded = pcm + bytes(8000*2)
        if len(padded) > 320000:
            raise ValueError('Padded input exceeds ten seconds')
        expected = read(BASE/group/'report.json')['prompt']
        raw_path, hp_path = out/(label+'-raw.pcm'), out/(label+'-hp300.pcm')
        raw_path.write_bytes(padded)
        command = ['wsl', '-d', 'Ubuntu', '--cd', root_posix, '--',
            './'+renderer.relative_to(ROOT).as_posix(), raw_path.relative_to(ROOT).as_posix(),
            hp_path.relative_to(ROOT).as_posix()]
        completed = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        (out/(label+'-renderer.stdout')).write_bytes(completed.stdout)
        (out/(label+'-renderer.stderr')).write_bytes(completed.stderr)
        completed.check_returncode()
        filtered = hp_path.read_bytes()
        if len(filtered) != len(padded):
            raise ValueError('Filter changed sample count')
        pair = dict(id=label, expected=expected, original_samples=original_samples,
            original_pcm_sha256=sha(pcm), source=str(source.relative_to(ROOT)),
            source_wav_sha256=sha(source.read_bytes()), source_report_sha256=sha((source_dir/'report.json').read_bytes()),
            renderer_command=command, analysis_interval_s=interval, variants={})
        for variant, data in (('raw', padded), ('hp300', filtered)):
            filename = f'{label}-{variant}.wav'
            write_wav(out/filename, data)
            # Text check excludes invented zero padding, never truncates original captured input.
            original = data[:len(pcm)]
            stream = recognizer.create_stream()
            stream.accept_waveform(16000, np.frombuffer(original, dtype='<i2').astype(np.float32)/32768)
            recognizer.decode_stream(stream)
            text = stream.result.text
            pair['variants'][variant] = dict(text=text, text_complete=complete_input(expected, text),
                full=metrics(original), analysis_window=metrics(data[int(interval[0]*32000):int(interval[1]*32000)]))
            result['fixtures'].append(dict(id=f'{label}-{variant}', expected=expected, input_file=filename,
                samples=len(data)//2, pcm_sha256=sha(data), original_samples=original_samples, extra_tail_ms=500,
                source=dict(path=str(source.relative_to(ROOT)), pcm_sha256=sha(pcm), variant=variant)))
        pair['analysis_window_change_db'] = 20*np.log10(max(pair['variants']['hp300']['analysis_window']['rms'],1e-20)/
            max(pair['variants']['raw']['analysis_window']['rms'],1e-20))
        result['pairs'].append(pair)
    result['cloud_prerequisites_pass'] = all(p['variants']['hp300']['text_complete'] and
        p['variants']['hp300']['full']['full_scale_samples'] == 0 for p in result['pairs'])
    (out/'report.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    print(json.dumps(dict(cloud_prerequisites_pass=result['cloud_prerequisites_pass'],
        pairs=[dict(id=p['id'], change_db=p['analysis_window_change_db'],
                    raw=p['variants']['raw']['text'], filtered=p['variants']['hp300']['text']) for p in result['pairs']]), ensure_ascii=False))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--renderer', type=Path, required=True)
    a = p.parse_args()
    prepare(a.out, a.renderer)
