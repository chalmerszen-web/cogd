"""One offline wake-phrase confirmation feasibility check on frozen board PCM.

No recording, USB, training, network, or model deployment. This host ASR model
cannot run on the C3. Passing here would only justify a separate assessment of
the existing streaming-ASR path, not certify a working offline wake repair.
"""
import argparse
import hashlib
import json
from pathlib import Path
import time
import wave

import numpy as np
import sherpa_onnx

ROOT = Path(__file__).resolve().parents[2]
RATE = 16000
PRETRIGGER_SAMPLES = 32768
WORD = '你好小言'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical(text):
    # Fixed before looking at results: punctuation only, no homophone expansion,
    # edit-distance matching, contextual text correction, or ASR hotwords.
    return ''.join(c for c in text if c.isalnum())


def wav_samples(path):
    with wave.open(str(path), 'rb') as f:
        if (f.getnchannels(), f.getsampwidth(), f.getframerate()) != (1, 2, RATE):
            raise ValueError('Frozen source must be mono 16-bit 16kHz WAV')
        return f.getnframes()


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf8')


def evaluate(parity, capture, model, out):
    out.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    source = json.loads(parity.read_text(encoding='utf8'))
    if not source['complete'] or not source['all_frames_equal'] or source['source_aligned'] != 24:
        raise ValueError('All original input/score/clock/alignment checks must have passed')
    selected = source['trials'][1:]
    if len(selected) != 24 or any(t['source']['split'] != 'train' for t in selected):
        raise ValueError('Only the fixed 24 labelled TRAIN public-source replays are allowed')
    plan = dict(stage='UX250', samples=24, source_parity_sha256=sha(parity),
        script_sha256=sha(Path(__file__)), model_sha256=sha(model / 'model.int8.onnx'),
        tokens_sha256=sha(model / 'tokens.txt'), language='auto', num_threads=4,
        isolated_crop='Previously frozen alignment lag,128ms before/after entire public source; no new alignment.',
        pretrigger_crop_samples=PRETRIGGER_SAMPLES,
        pretrigger_crop='Only first original C event; exactly preceding2.048s; no lookahead or post-trigger suffix.',
        phrase_rule='Canonical transcript starts with exactly 你好小言; remove punctuation only.',
        gates='Keep all12 existing positives across zh/yue and reject the one known near-word false wake; any loss or retained false wake stops this path.',
        private_ambient_excluded=True, frozen_TEST_not_read=True, no_device_or_network=True,
        result_does_not_prove_C3_ASR_or_field_false_wake_rate=True,
        maximum_seconds=180, rows=[])
    save(out / 'plan.json', {k: v for k, v in plan.items() if k != 'rows'})
    recognizer = sherpa_onnx.OfflineRecognizer.from_sense_voice(
        model=str(model / 'model.int8.onnx'), tokens=str(model / 'tokens.txt'),
        num_threads=4, use_itn=True, language='auto')
    report = dict(plan, complete=False, started_at=time.time())
    try:
        for t in selected:
            if time.monotonic() - started > 180:
                raise TimeoutError('The declared finite180s budget was exhausted')
            raw_path = capture / t['trial'] / 'input.pcm'
            raw = raw_path.read_bytes()
            if hashlib.sha256(raw).hexdigest() != t['pcm_sha256']:
                raise ValueError('Original board PCM changed')
            pcm = np.frombuffer(raw, dtype='<i2')
            meta = t['source']
            public_path = ROOT / meta['path']
            count = wav_samples(public_path)
            lag = t['lag_samples']
            lo, hi = max(0, lag - 2048), min(len(pcm), lag + count + 2048)
            if not t['aligned'] or hi - lo < count:
                raise ValueError('The frozen alignment cannot contain the whole source')
            row = dict(trial=t['trial'], source_id=meta['clip_id'], language=meta['language'],
                label=meta['label'], public_text=meta['text'], original_pcm_sha256=t['pcm_sha256'],
                source_wav_sha256=sha(public_path), source_samples=count, lag_samples=lag,
                original_C_events=t['events_samples'], cases=[])
            cases = [('isolated_full_source', lo, hi)]
            if t['events_samples']:
                at = t['events_samples'][0]
                cases.append(('first_C_event_pretrigger', max(0, at - PRETRIGGER_SAMPLES), at))
            for name, lo, hi in cases:
                clip = pcm[lo:hi]
                stream = recognizer.create_stream()
                begin = time.monotonic()
                stream.accept_waveform(RATE, clip.astype(np.float32) / 32768)
                recognizer.decode_stream(stream)
                text = stream.result.text.strip()
                row['cases'].append(dict(kind=name, samples=[lo, hi],
                    crop_sha256=hashlib.sha256(clip.tobytes()).hexdigest(),
                    whole_source_available=(lo <= lag and hi >= lag + count),
                    text=text, canonical=canonical(text),
                    accepted=canonical(text).startswith(WORD),
                    inference_seconds=time.monotonic() - begin))
            report['rows'].append(row)
            save(out / 'report.json', report)
            print(json.dumps(dict(trial=row['trial'], language=row['language'], label=row['label'],
                cases=[dict(kind=c['kind'], text=c['text'], accepted=c['accepted'],
                            whole_source_available=c['whole_source_available']) for c in row['cases']]),
                ensure_ascii=False), flush=True)
        report['summary'] = {}
        for language in ('zh', 'yue'):
            rows = [r for r in report['rows'] if r['language'] == language]
            pos = [r for r in rows if r['label']]
            neg = [r for r in rows if not r['label']]
            report['summary'][language] = dict(positives=len(pos), negatives=len(neg),
                isolated_positive_accepted=sum(r['cases'][0]['accepted'] for r in pos),
                original_positive_wakes=sum(bool(r['original_C_events']) for r in pos),
                pretrigger_positive_accepted=sum(len(r['cases']) == 2 and r['cases'][1]['accepted'] for r in pos),
                original_negative_wakes=sum(bool(r['original_C_events']) for r in neg),
                pretrigger_negative_accepted=sum(len(r['cases']) == 2 and r['cases'][1]['accepted'] for r in neg),
                isolated_negative_accepted=sum(r['cases'][0]['accepted'] for r in neg))
        report['gate_passed'] = all(s['isolated_positive_accepted'] == s['positives'] and
            s['pretrigger_positive_accepted'] == s['original_positive_wakes'] and
            s['pretrigger_negative_accepted'] == 0 for s in report['summary'].values())
        report['complete'] = True
    except Exception as e:
        report['error'] = repr(e)
        raise
    finally:
        report['seconds'] = time.monotonic() - started
        report['ended_at'] = time.time()
        report['deployed'] = False
        report['false_wake_fixed'] = False
        save(out / 'report.json', report)
    print(json.dumps(dict(summary=report['summary'], gate_passed=report['gate_passed'],
        seconds=report['seconds'], deployed=False, false_wake_fixed=False), ensure_ascii=False), flush=True)
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--parity', type=Path, required=True)
    p.add_argument('--capture', type=Path, required=True)
    p.add_argument('--model', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    evaluate(a.parity, a.capture, a.model, a.out)
