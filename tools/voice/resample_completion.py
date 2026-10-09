"""Re-export receipted light speech offline; no credentials, network or playback."""
import argparse
import json
from pathlib import Path
import shutil

from generate_progress import ROOT, digest, encode, json_write, run, wav_pcm


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg:
        raise RuntimeError('FFmpeg required')
    source = args.source/'manifest.json'
    original = json.loads(source.read_text(encoding='utf8'))
    assert original['complete'] and original['kind'] == 'completion'
    assert original['sample_rate'] == 24000 and len(original['clips']) == 2
    args.out.mkdir(parents=True, exist_ok=False)
    lines = ['/* Offline resample: tools/voice/resample_completion.py; original TTS receipts retained. */',
             'enum { COMPLETION_RATE=16000 };']
    report = dict(sample_rate=16000, billable_posts=0, complete=False, clips=[],
                  source_manifest_sha256=digest(source), script_sha256=digest(__file__))
    for clip in original['clips']:
        name = clip['name']
        assert name in ('completion_zh', 'completion_yue')
        pcm = args.source/name/'pcm-speed-1.15.wav'
        assert digest(pcm) == clip['source_pcm_sha256']
        before = len(wav_pcm(pcm, 24000))//2
        assert before == clip['samples']
        folder = args.out/name
        folder.mkdir()
        run([ffmpeg, '-v', 'error', '-i', pcm, '-ar', '16000', '-ac', '1',
             '-c:a', 'pcm_s16le', folder/'trimmed.wav'], folder/'resample.log')
        raw, info = encode(ffmpeg, folder, 1.0, 16000)
        assert abs(info['seconds']-before/24000) <= 1/16000
        lines += [f'static const uint8_t {name}[] = {{']
        lines += ['    '+','.join(f'0x{x:02x}' for x in raw[i:i+20])+',' for i in range(0,len(raw),20)]
        lines += ['};', f'enum {{ {name.upper()}_SAMPLES={info["samples"]}, {name.upper()}_BLOCK={info["block_bytes"]} }};']
        report['clips'].append(dict(name=name, text=clip['text'], source=str(pcm),
            source_samples=before, original_pcm_sha256=digest(pcm),
            original_provider_request_id=clip['provider_request_id'], **info))
    lines += ['_Static_assert(sizeof(completion_zh)+sizeof(completion_yue)<=32768u,"Speech asset budget");']
    header = ROOT/'plugins/audio/completion_assets.h'
    header.write_text('\n'.join(lines)+'\n', encoding='utf8')
    report.update(complete=True, header_sha256=digest(header),
                  asset_bytes=sum(c['asset_bytes'] for c in report['clips']))
    json_write(args.out/'manifest.json', report)
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()
