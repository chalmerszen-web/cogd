"""Generate two receipted Qwen progress clips once; export verified Flash IMA data.

Cloud creation requires --execute. Existing task directories are reused by the
skill's one-POST guard; failures/unknown receipts are never resubmitted. Local
processing is repeatable and never plays audio or accesses USB. Requires FFmpeg.
"""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import wave
import zlib

ROOT = Path(__file__).resolve().parents[2]
MODEL = 'qwen3-tts-flash-2025-11-27'
CLIPS = [('mandarin', '收到，我来处理。', 'Cherry'),
         ('cantonese', '收到，等我处理。', 'Kiki')]
SOURCES = ['https://help.aliyun.com/zh/model-studio/qwen-tts-api',
           'https://help.aliyun.com/en/model-studio/qwen-tts-voice-list']
BUDGET = 20 * 1024


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def json_write(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf8')


def run(command, log):
    result = subprocess.run([str(v) for v in command], capture_output=True)
    log.write_bytes(result.stdout+b'\n'+result.stderr)
    if result.returncode:
        raise RuntimeError(f'Command failed ({result.returncode}); see {log}')
    return result.stdout


def wav_pcm(path, sample_rate=16000):
    with wave.open(str(path), 'rb') as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (1, 2, sample_rate):
            raise ValueError(f'Expected {sample_rate}-Hz mono PCM16')
        return wav.readframes(wav.getnframes())


def save_pcm(path, data, sample_rate=16000):
    with wave.open(str(path), 'wb') as wav:
        wav.setparams((1, 2, sample_rate, 0, 'NONE', 'not compressed'))
        wav.writeframes(data)


def chunks(path):
    data = path.read_bytes()
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError('Expected RIFF WAVE')
    result = {}
    pos = 12
    while pos+8 <= len(data):
        name, size = struct.unpack_from('<4sI', data, pos)
        pos += 8
        if size > len(data)-pos:
            raise ValueError('Truncated RIFF chunk')
        result[name] = data[pos:pos+size]
        pos += size+(size & 1)
    return result


def trim_edges(data):
    pcm = array.array('h', data)
    if sys.byteorder != 'little':
        pcm.byteswap()
    threshold = max(8, max(abs(v) for v in pcm)*.003)
    active = [i for i, value in enumerate(pcm) if abs(value) > threshold]
    if not active:
        raise ValueError('Silent synthesis')
    lo, hi = max(0, active[0]-400), min(len(pcm), active[-1]+1+960)
    return data[lo*2:hi*2], {'begin_sample': lo, 'end_sample': hi,
                           'threshold_pcm': threshold, 'padding_samples': [400, 960]}


def encode(ffmpeg, folder, speed, sample_rate=16000):
    target = folder/f'pcm-speed-{speed:.2f}.wav'
    tempo = ['-af', f'atempo={speed:.2f}'] if speed != 1.0 else []
    run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-i', folder/'trimmed.wav',
         *tempo, '-c:a', 'pcm_s16le', target], folder/'tempo.log')
    pcm = wav_pcm(target, sample_rate)
    encoded = folder/f'ima-speed-{speed:.2f}.wav'
    run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-i', target,
         '-c:a', 'adpcm_ima_wav', encoded], folder/'encode.log')
    parts = chunks(encoded)
    fmt = struct.unpack_from('<HHIIHH', parts[b'fmt '])
    if fmt[0] != 17 or fmt[1] != 1 or fmt[2] != sample_rate or fmt[5] != 4:
        raise ValueError('Unexpected IMA WAV format')
    block = fmt[4]
    per_block = 1+2*(block-4)
    samples = len(pcm)//2
    complete, tail = divmod(samples, per_block)
    stored = complete*block+(4+math.ceil((tail-1)/2) if tail else 0)
    raw = parts[b'data'][:stored]
    if len(raw) != stored:
        raise ValueError('Encoded sample count is short')
    decoded = folder/f'ffmpeg-reference-{speed:.2f}.wav'
    run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-i', encoded,
         '-c:a', 'pcm_s16le', decoded], folder/'decode.log')
    reference = wav_pcm(decoded, sample_rate)[:len(pcm)]
    if len(reference) != len(pcm):
        raise ValueError('Reference decoder sample count is short')
    (folder/'reference.pcm').write_bytes(reference)
    (folder/'asset.ima').write_bytes(raw)
    return raw, {'samples': samples, 'seconds': samples/sample_rate, 'block_bytes': block,
                 'speed': speed, 'asset_bytes': stored,
                 'asset_sha256': hashlib.sha256(raw).hexdigest(),
                 'reference_pcm_sha256': hashlib.sha256(reference).hexdigest(),
                 'reference_pcm_crc32': f'{zlib.crc32(reference):08x}',
                 'encoded_wav_sha256': digest(encoded), 'source_pcm_sha256': digest(target)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--source', type=Path, default=ROOT/'plugins/audio/progress.c')
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--ffmpeg', default=shutil.which('ffmpeg'))
    parser.add_argument('--skill', type=Path, default=Path.home()/'.codex/skills/qianwen-model-suite')
    args = parser.parse_args()
    if not args.ffmpeg:
        raise RuntimeError('FFmpeg must be installed explicitly')
    args.out.mkdir(parents=True, exist_ok=True)
    runner = [sys.executable, '-X', 'utf8', args.skill/'scripts/qianwen.py']
    manifest = dict(model=MODEL, synthetic=True, cloned_voice=False, sample_rate=16000,
                    budget_bytes=BUDGET, official_sources=SOURCES, clips=[], complete=False)
    for name, text, voice in CLIPS:
        folder = args.out/name
        folder.mkdir(exist_ok=True)
        task = dict(capability='tts-qwen', request=dict(model=MODEL,
                    input=dict(text=text, voice=voice, language_type='Chinese')))
        task_path = folder/'task.json'
        if task_path.exists() and json.loads(task_path.read_text(encoding='utf8')) != task:
            raise RuntimeError('Existing generation task differs; preserved without submission')
        json_write(task_path, task)
        run(runner+['plan', '--task', task_path], folder/'plan.log')
        state = folder/'job/state.json'
        if not state.exists() and not args.execute:
            print(f'{name}: planned; --execute required for one generation', flush=True)
            continue
        if not state.exists():
            run(runner+['run', '--task', task_path, '--state-dir', folder/'job', '--execute'], folder/'run.log')
        status = json.loads(state.read_text(encoding='utf8'))
        if status['status'] != 'succeeded':
            raise RuntimeError(f'{name}: preserved provider state {status["status"]}; no retry')
        run(runner+['collect', '--state-dir', folder/'job'], folder/'collect.log')
        assets = json.loads((folder/'job/artifacts/assets.json').read_text(encoding='utf8'))['files']
        if len(assets) != 1:
            raise RuntimeError('Expected exactly one synthesized audio asset')
        original = folder/'job/artifacts'/assets[0]['file']
        canonical = folder/'original-16k.wav'
        run([args.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-i', original,
             '-ac', '1', '-ar', '16000', '-c:a', 'pcm_s16le', canonical], folder/'resample.log')
        trimmed, trim = trim_edges(wav_pcm(canonical))
        save_pcm(folder/'trimmed.wav', trimmed)
        receipt = json.loads((folder/'job/result.private.json').read_text(encoding='utf8'))
        manifest['clips'].append(dict(name=name, text=text, voice=voice, model=MODEL,
            original_sha256=digest(original), original_file=str(original.relative_to(args.out)),
            original_16k_sha256=digest(canonical), trim=trim, task_sha256=digest(task_path),
            provider_request_id=receipt.get('request_id'), billable_posts=status.get('billable_posts')))
        print(f'{name}: receipted audio available, trimmed {len(trimmed)/32000:.3f}s', flush=True)
    json_write(args.out/'manifest.json', manifest)
    if len(manifest['clips']) != 2:
        return
    data = []
    for speed in (1.0, 1.15, 1.25, 1.35):
        data = [encode(args.ffmpeg, args.out/row['name'], speed) for row in manifest['clips']]
        if sum(len(raw) for raw, _ in data) <= BUDGET:
            break
    else:
        raise RuntimeError('Full phrases exceed 20 KiB at maximum 1.35x; preserve originals for an explicit shorter edit')
    block_sizes = {info['block_bytes'] for _, info in data}
    if len(block_sizes) != 1:
        raise RuntimeError('IMA block sizes differ')
    generated = ['/* BEGIN GENERATED PROGRESS ASSETS */',
                 f'/* Qwen {MODEL}, built-in Cherry/Kiki. Reproduce: tools/voice/generate_progress.py */']
    for row, (raw, info) in zip(manifest['clips'], data):
        row.update(info)
        generated += [f'/* {row["text"]} SHA256 {row["asset_sha256"]} */',
                      f'static const uint8_t {row["name"]}[] = {{']
        generated += ['    '+','.join(f'0x{v:02x}' for v in raw[i:i+20])+',' for i in range(0,len(raw),20)]
        generated += ['};']
    generated += [f'enum {{ MANDARIN_SAMPLES={data[0][1]["samples"]}, CANTONESE_SAMPLES={data[1][1]["samples"]}, PROGRESS_BLOCK_BYTES={block_sizes.pop()} }};',
                  '_Static_assert(sizeof(mandarin)+sizeof(cantonese)<=20u*1024u,"Progress asset Flash budget");',
                  '/* END GENERATED PROGRESS ASSETS */']
    source = args.source.read_text(encoding='utf8')
    source, count = re.subn(r'/\* BEGIN GENERATED PROGRESS ASSETS \*/.*?/\* END GENERATED PROGRESS ASSETS \*/',
                            lambda _: '\n'.join(generated), source, flags=re.S)
    if count != 1:
        raise RuntimeError('Expected one generated section')
    args.source.write_text(source, encoding='utf8')
    manifest.update(complete=True, total_asset_bytes=sum(len(raw) for raw, _ in data),
                    generated_c_sha256=digest(args.source), ffmpeg_version=subprocess.check_output(
                        [args.ffmpeg, '-version'], text=True).splitlines()[0])
    json_write(args.out/'manifest.json', manifest)
    print(json.dumps(dict(complete=True, bytes=manifest['total_asset_bytes'],
          clips=[{key: row[key] for key in ('name','seconds','speed','reference_pcm_crc32')} for row in manifest['clips']]),ensure_ascii=False))


if __name__ == '__main__':
    main()
