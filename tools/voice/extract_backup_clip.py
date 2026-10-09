"""Read a verified post-trial backup, retaining committed clip provenance.

No USB/network. Production C parser and independent Python v3 decoding must
agree. Matching sample counts are not a recorded upload PCM checksum.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import wave
import zlib

from recover_uploaded_clip import decode_v3


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf8'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--backup', type=Path, required=True)
    p.add_argument('--group', type=Path, required=True)
    p.add_argument('--status', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--decoder', type=Path, required=True,
                   help='Native production-C reader executable')
    a = p.parse_args()
    if a.out.exists():
        raise FileExistsError(a.out)
    manifest = read(a.backup/'manifest.json')
    image = (a.backup/'flash-before.bin').read_bytes()
    if (len(image) != 0x400000 or not manifest['backup_device_verified'] or
        sha(a.backup/'flash-before.bin') != manifest['backup_sha256']):
        raise ValueError('Invalid verified backup')
    report = read(a.group/'report.json')
    status = read(a.status)
    trial = report['trials'][-1]
    trace = [json.loads(e['text']) for e in trial['events'] if e['stage'] == 'capture_tail_v2']
    uploads = [json.loads(e['text']) for e in trial['events'] if e['stage'] == 'asr_upload_stats']
    if len(trace) != 1 or len(uploads) != 1 or trace[0][10] or not uploads[0]['complete']:
        raise ValueError('Need one complete, successful final capture and upload')
    part = manifest['partitions']['clip']
    begin, size = part['offset'], part['size']
    if size != 0x70000 or not 0 <= begin < begin+size <= len(image):
        raise ValueError('Clip geometry disagrees')
    clip = image[begin:begin+size]
    magic, version, rate, samples, encoded_crc, stored, header_crc, commit = struct.unpack('<8I', clip[:32])
    if (magic != 0x31504341 or version != 3 or rate != 16000 or commit != 0x54494d43 or
        header_crc != zlib.crc32(clip[:24]) or not 0 < samples <= 160000 or
        not (samples+1)//2 <= stored <= size-32):
        raise ValueError('Invalid committed v3 header')
    if (samples != trace[0][9] or samples != uploads[0]['samples'] or
        not status['audio']['clip_ready'] or status['audio']['capture_samples'] != samples or
        not status['audio']['record_input_valid']):
        raise ValueError('Backup clip and final trial observations disagree')
    body = clip[32:32+stored]
    if zlib.crc32(body) != encoded_crc:
        raise ValueError('Encoded body CRC mismatch')
    raw, values, consumed, records = decode_v3(body, samples)
    a.out.mkdir(parents=True)
    partition = a.out/'clip-partition.bin'
    partition.write_bytes(clip)
    c_output = a.out/'device-clip.pcm'
    result = subprocess.run([str(a.decoder.resolve()), str(partition.resolve()),
                             str(c_output.resolve()), '127'], capture_output=True, text=True)
    if result.returncode:
        raise ValueError('Production C decoder rejected clip: '+result.stderr)
    c_meta = json.loads(result.stdout)
    if c_output.read_bytes() != raw or c_meta['samples'] != samples:
        raise ValueError('Independent C/Python PCM disagreement')
    target = a.out/'device-clip.wav'
    with wave.open(str(target), 'wb') as wav:
        wav.setparams((1, 2, rate, samples, 'NONE', 'not compressed'))
        wav.writeframes(raw)
    evidence = dict(round=trial['round'], firmware=report['before']['version'],
        header_committed=True, header_crc_verified=True, encoded_crc_verified=True,
        independent_c_python_equal=True, samples=samples, encoded_crc32=f'{encoded_crc:08x}',
        decoded_pcm_crc32=f'{zlib.crc32(raw):08x}', pcm_sha256=sha(c_output),
        wav_sha256=sha(target), stored_bytes=stored, prefix_bytes_decoded=consumed,
        decoded_records=records, production_c=c_meta, upload_samples_match=True,
        logged_upload_pcm_crc32=uploads[0].get('pcm_crc32'),
        peak=max(abs(v) for v in values), clipped_samples=sum(abs(v) >= 32767 for v in values),
        input_hashes={str(path): sha(path) for path in (a.backup/'flash-before.bin',
            a.backup/'manifest.json', a.group/'report.json', a.status, a.decoder,
            Path(__file__), Path(__file__).with_name('recover_uploaded_clip.py'))},
        limitations=['Stored last committed clip from verified post-trial backup; matching samples and logs, no embedded turn ID.',
                     'No logged PCM checksum on this ASR upload. No byte-identical upload or cloud-processing claim.',
                     'Encoded CRC and committed header are verified; PCM checksum is newly computed diagnostic evidence.'])
    with (a.out/'report.json').open('x', encoding='utf8') as f:
        f.write(json.dumps(evidence, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps({key:evidence[key] for key in ('samples', 'header_committed',
        'independent_c_python_equal', 'encoded_crc_verified', 'logged_upload_pcm_crc32')}, ensure_ascii=False))


if __name__ == '__main__':
    main()
