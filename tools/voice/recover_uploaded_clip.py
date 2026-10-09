"""Recover only a CRC-verified uploaded prefix from an aborted packed-v3 clip.

Reads a flash_guard backup; never accesses or changes the device. An erased
header remains invalid. The recovered WAV is diagnostic evidence, not a
successfully committed clip or proof that the cloud processed the upload.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import wave
import zlib


def sha(data):
    return hashlib.sha256(data).hexdigest()


def decode_v3(body, samples):
    if not 0 < samples <= 160000:
        raise ValueError('Invalid bounded sample count')
    values, at, records = [], 0, 0
    while len(values) < samples:
        if at >= len(body):
            raise ValueError('Truncated record header')
        header = body[at]
        at += 1
        count = (header & 127) + 1
        if not header & 128:
            end = at + count * 2
            if end > len(body):
                raise ValueError('Truncated raw record')
            values.extend(struct.unpack_from(f'<{count}h', body, at))
            at = end
        else:
            if at + 3 > len(body):
                raise ValueError('Truncated delta record')
            width = body[at]
            if width not in (4, 6, 8):
                raise ValueError(f'Invalid delta width {width} at {at}')
            previous = struct.unpack_from('<h', body, at + 1)[0]
            at += 3
            values.append(previous)
            bits, available = 0, 0

            def take(n):
                nonlocal at, bits, available
                while available < n:
                    if at >= len(body):
                        raise ValueError('Truncated delta token')
                    bits |= body[at] << available
                    at += 1
                    available += 8
                result = bits & ((1 << n) - 1)
                bits >>= n
                available -= n
                return result

            escape = (1 << width) - 1
            for _ in range(count - 1):
                token = take(width)
                if token == escape:
                    raw = take(16)
                    current = raw - 65536 if raw >= 32768 else raw
                else:
                    current = previous + (token - escape // 2) * 16
                if not -32768 <= current <= 32767:
                    raise ValueError('Delta sample overflow')
                values.append(current)
                previous = current
            if bits:
                raise ValueError('Nonzero record padding')
        records += 1
    values = values[:samples]
    return struct.pack(f'<{samples}h', *values), values, at, records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backup', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        raise FileExistsError(args.out)
    manifest_bytes = (args.backup / 'manifest.json').read_bytes()
    manifest = json.loads(manifest_bytes)
    flash = (args.backup / 'flash-before.bin').read_bytes()
    if sha(flash) != manifest['backup_sha256'] or not manifest['backup_device_verified']:
        raise ValueError('Backup verification failed')
    report_bytes = args.report.read_bytes()
    report = json.loads(report_bytes)
    trial = report['trials'][-1]
    upload = json.loads(next(e['text'] for e in trial['events'] if e['stage'] == 'fast_upload_stats'))
    clip = manifest['partitions']['clip']
    offset, size = clip['offset'], clip['size']
    if not 0 <= offset < offset + size <= len(flash) or size < 32:
        raise ValueError('Clip outside backup')
    header = flash[offset:offset + 32]
    if header != b'\xff' * 32:
        raise ValueError('Expected an erased, uncommitted header')
    raw, values, consumed, records = decode_v3(flash[offset + 32:offset + size], upload['samples'])
    measured = dict(peak=max(abs(v) for v in values),
                    mean_abs=sum(abs(v) for v in values) // len(values),
                    zero_samples=values.count(0))
    crc = f'{zlib.crc32(raw):08x}'
    if crc != upload['pcm_crc32'] or any(upload[k] != v for k, v in measured.items()):
        raise ValueError('Recovered prefix disagrees with actual upload')
    args.out.mkdir(parents=True)
    (args.out / 'uploaded-prefix.pcm').write_bytes(raw)
    with wave.open(str(args.out / 'uploaded-prefix.wav'), 'wb') as wav:
        wav.setparams((1, 2, 16000, len(values), 'NONE', 'not compressed'))
        wav.writeframes(raw)
    result = dict(firmware=report['before']['version'], round=trial['round'],
                  backup=str(args.backup), backup_sha256=sha(flash),
                  manifest_sha256=sha(manifest_bytes), report_sha256=sha(report_bytes),
                  script_sha256=sha(Path(__file__).read_bytes()), packed_version=3,
                  header_committed=False, decoded_records=records, body_bytes=consumed,
                  samples=len(values), crc32=crc, crc_matches=True,
                  amplitude=measured, amplitude_matches=True, pcm_sha256=sha(raw),
                  wav_sha256=sha((args.out / 'uploaded-prefix.wav').read_bytes()),
                  limitation='Recovered only the uploaded prefix of an aborted clip. CRC and amplitude match device WebSocket-encoder input; no claim of a committed clip or cloud processing.')
    (args.out / 'recovery.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
