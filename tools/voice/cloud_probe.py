"""Bounded provider probes using the installed Vocalign client; never print keys."""
import argparse
import json
from pathlib import Path
import sys
import time
import wave
from credentials import platform_key

ROOT = Path(__file__).resolve().parents[2]
SKILL = Path.home() / '.codex/skills/vocaligntech/scripts'
sys.path.insert(0, str(SKILL))
from transport import HTTP
from vocalign import validate_task, upload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--key-file', type=Path, default=Path.home()/'Desktop/key.txt')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--text', default='小言已经准备好了。请把四颗灯设为蓝色，然后告诉我现在的颜色。')
    parser.add_argument('--asr', action='store_true')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    report = {'complete': False, 'started': time.time(), 'synthetic': True}
    def save():
        (args.out/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    save()
    http = HTTP(platform_key(args.key_file))
    choice = json.loads((ROOT/'artifacts/voice-cloud/voice-choice.json').read_text())
    task = validate_task({'operation':'speech', 'voice':choice['voice'], 'input':args.text,
                          'response_format':'wav', 'speed':1.0, 'output':'input.wav'}, args.out.resolve())
    report['tts_submitting'] = True
    save()
    # No retry after an uncertain paid POST. A failed probe is retained as-is.
    http.download('/v1/audio/speech', args.out/'input.wav', authenticated=True, method='POST',
                  payload={'model':'openai-tts', **{k:task[k] for k in ('voice','input','response_format','speed')}})
    with wave.open(str(args.out/'input.wav'), 'rb') as audio:
        pcm = audio.readframes(48000*90)
        report['wave'] = dict(rate=audio.getframerate(), channels=audio.getnchannels(),
                              width=audio.getsampwidth(), declared_frames=audio.getnframes(),
                              frames=len(pcm)//(audio.getnchannels()*audio.getsampwidth()))
    shape=report['wave']
    with wave.open(str(args.out/'input-canonical.wav'), 'wb') as audio:
        audio.setparams((shape['channels'],shape['width'],shape['rate'],0,'NONE','not compressed'))
        audio.writeframes(pcm)
    duration=(shape['frames']+shape['rate']-1)//shape['rate']
    report['tts_finished'] = time.time()
    save()
    if args.asr:
        source = upload(http, 'qwen-asr', str((args.out/'input-canonical.wav').resolve()))
        result = http.json('POST', '/v1/audio/transcriptions', payload={
            'model':'qwen-asr','file_url':source,'duration':duration})
        report['asr_id'] = result['id']
        save()
        deadline = time.monotonic()+180
        while result.get('status') not in ('completed','failed') and time.monotonic()<deadline:
            time.sleep(3)
            result = http.json('GET', '/v1/audio/transcriptions/'+report['asr_id'])
        report['asr_status'] = result.get('status')
        if result.get('status') == 'completed':
            http.download(result['transcription_url'], args.out/'asr.json', authenticated=False)
            obj = json.loads((args.out/'asr.json').read_text(encoding='utf8'))
            report['transcript'] = ' '.join(t['text'] for t in obj['transcripts'])
        save()
        assert result.get('status') == 'completed', report['asr_status']
    report['complete'] = True
    report['finished'] = time.time()
    save()
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()
