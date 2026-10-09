"""Check saved negotiated PCM with local ASR; no provider, USB or playback."""
import argparse
import hashlib
import json
from pathlib import Path
import wave

import numpy as np
import sherpa_onnx


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    target = args.directory/'output-analysis.json'
    if target.exists():
        raise FileExistsError(target)
    report = json.loads((args.directory/'report.json').read_text(encoding='utf8'))
    rate = report['output_rate']
    assert report['complete'] and report['attempts'] == 1 and report['retries'] == 0
    assert report['output_format_echo'] == dict(type='pcm', sample_rate=rate)
    recognizer = sherpa_onnx.OfflineRecognizer.from_sense_voice(
        model='artifacts/kws-phase2/asr/model.int8.onnx',
        tokens='artifacts/kws-phase2/asr/tokens.txt', num_threads=4,
        use_itn=True, language='auto')
    rows = []
    for turn in report['turns']:
        path = args.directory/(turn['id']+'.wav')
        with wave.open(str(path)) as wav:
            assert wav.getparams()[:3] == (1, 2, rate)
            raw = wav.readframes(wav.getnframes())
        assert hashlib.sha256(raw).hexdigest() == turn['pcm_sha256']
        pcm = np.frombuffer(raw, dtype='<i2')
        stream = recognizer.create_stream()
        stream.accept_waveform(rate, pcm.astype(np.float32)/32768)
        recognizer.decode_stream(stream)
        rows.append(dict(id=turn['id'], sample_rate=rate, samples=len(pcm),
            duration_s=len(pcm)/rate, peak=int(np.max(np.abs(pcm.astype(np.int32)))),
            full_scale_samples=int(np.sum((pcm == -32768) | (pcm == 32767))),
            local_asr=stream.result.text, provider_text=turn['text'],
            wav_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    result = dict(accepted=False, protocol_complete=True, rows=rows,
        scope='Saved synthetic protocol output only; no device latency or subjective quality claim',
        source_sha256=hashlib.sha256((args.directory/'report.json').read_bytes()).hexdigest(),
        script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    target.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
