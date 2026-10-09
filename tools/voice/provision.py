"""Provision the voice provider from the user's local key file; keep Wi-Fi/DeepSeek."""
import argparse
import json
from pathlib import Path
from credentials import platform_key
from device import Device,ROOT


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--key-file',type=Path,default=Path.home()/'Desktop/key.txt')
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--enable',action='store_true')
    p.add_argument('--voice',help='An existing ready provider voice ID; otherwise reuse the local selected voice')
    a=p.parse_args();key=platform_key(a.key_file)
    voice=a.voice or json.loads((ROOT/'artifacts/voice-cloud/voice-choice.json').read_text())['voice']
    device=Device(a.out)
    try:
        status=device.command('agent status',query=True)
        assert status['version']=='0.8.0-voice-exp',status.get('version')
        device.command('agent voice off')
        device.command('agent voice configure '+json.dumps({'api_key':key,'voice':voice}),secret=True)
        device.command('agent context budget 200',timeout=60)
        context=device.command('agent context stats',query=True)
        assert context['history_budget']==204800
        if a.enable:device.command('agent voice on')
        voice=device.command('agent voice status',query=True)
        print(json.dumps({'voice':voice,'history_budget':context['history_budget']},ensure_ascii=False))
    finally:device.save();device.close()


if __name__=='__main__':main()
