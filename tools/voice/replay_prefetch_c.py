"""Offline C replay of saved prefetch metadata plus the matching captured PCM.

No network, microphone, playback or tools. Reconstructs stripped audio deltas
from response WAVs; this verifies schemas/PCM, not original JSON key ordering.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import wave


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=Path('artifacts/voice-fast/host-handoff-agent/test_prefetch'))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    plan = json.loads((args.source/'report.json').read_text(encoding='utf8'))
    result = dict(scope='offline_reconstructed_wire', source_report_sha256=hashlib.sha256(
        (args.source/'report.json').read_bytes()).hexdigest(), attempts=[])
    for case in plan['attempts']:
        folder = args.source/case['id']
        report = json.loads((folder/'report.json').read_text(encoding='utf8'))
        pcm = {};offsets = {}
        for row in report['outputs']:
            source = Path(row['path'])
            with wave.open(str(source),'rb') as wav:
                assert (wav.getframerate(),wav.getnchannels(),wav.getsampwidth())==(24000,1,2)
                data = wav.readframes(wav.getnframes())
            assert hashlib.sha256(data).hexdigest()==row['pcm_sha256']
            pcm[row['response_id']] = data;offsets[row['response_id']] = 0
        wire = args.out/(case['id']+'.jsonl')
        with wire.open('x',encoding='utf8') as output:
            for line in (folder/'events.jsonl').read_text(encoding='utf8').splitlines():
                saved = json.loads(line);event = saved['event']
                if event['type']=='response.audio.delta':
                    rid = event['response_id'];at = offsets[rid];n = saved['audio_pcm_bytes']
                    chunk = pcm[rid][at:at+n];assert len(chunk)==n
                    event['delta'] = base64.b64encode(chunk).decode('ascii');offsets[rid] += n
                output.write(json.dumps(event,ensure_ascii=False,separators=(',',':'))+'\n')
        assert all(offsets[rid]==len(data) for rid,data in pcm.items())
        command = ['wsl','-d','Ubuntu','--cd',str(Path.cwd()),'--',
                   args.binary.as_posix(),wire.as_posix(),str(case['input_samples'])]
        run = subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
        (args.out/(case['id']+'.log')).write_bytes(run.stdout)
        result['attempts'].append(dict(id=case['id'],returncode=run.returncode,
            wire_sha256=hashlib.sha256(wire.read_bytes()).hexdigest()))
        (args.out/'report.json').write_text(json.dumps(result,indent=2),encoding='utf8')
        print(case['id'],run.returncode,flush=True)
        if run.returncode:raise RuntimeError('C replay failed; inspect the preserved case log')


if __name__=='__main__':
    main()
