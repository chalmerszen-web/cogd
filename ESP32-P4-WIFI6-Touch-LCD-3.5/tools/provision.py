"""Provision an identified P4 from explicitly supplied environment credentials.

Set WIFI_PASSWORD, DEEPSEEK_API_KEY and DASHSCOPE_API_KEY (or QIANWEN_API_KEY).
Supply --ssid / WIFI_SSID and --device-id / COGD_DEVICE_ID. No credential files
are read, and provisioning commands are redacted from device logs.
Success requires Wi-Fi plus natural-mode official local wake listening.
"""
import argparse
import json
import os
from pathlib import Path
import secrets
import sys
import time


def wait_for_listening(device, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        voice = device.command('agent voice status', query=True,
                               timeout=min(5, deadline - time.monotonic()))
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        wake = device.command('agent wake status', query=True, timeout=min(5, remaining))
        if wake.get('model') != 'wn10_nihaoxiaozhi' or wake.get('word') != '你好小智':
            raise RuntimeError('Expected the P4 official WakeNet10 / 你好小智 firmware')
        if wake.get('state') == 'failed':
            raise RuntimeError('Official wake listener failed to start')
        if (voice.get('voice_enabled') and voice.get('mode') == 'natural'
                and wake.get('enabled') and wake.get('state') == 'listening'):
            return voice, wake
        remaining = deadline - time.monotonic()
        if remaining > 0:
            time.sleep(min(0.5, remaining))
    raise TimeoutError(f'Natural-mode official wake listening did not become ready within {timeout:g} seconds')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM3')
    parser.add_argument('--ssid', default=os.environ.get('WIFI_SSID'))
    parser.add_argument('--device-id', default=os.environ.get('COGD_DEVICE_ID'),
                        help='Expected device_id from agent status; checked before changing configuration')
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--init-context', action='store_true')
    args = parser.parse_args()
    if not args.ssid:
        parser.error('Supply --ssid or WIFI_SSID')
    if not args.device_id:
        parser.error('Supply --device-id or COGD_DEVICE_ID')
    values = {
        'WIFI_PASSWORD': os.environ.get('WIFI_PASSWORD'),
        'DEEPSEEK_API_KEY': os.environ.get('DEEPSEEK_API_KEY'),
        'DASHSCOPE_API_KEY': os.environ.get('DASHSCOPE_API_KEY') or os.environ.get('QIANWEN_API_KEY'),
    }
    missing = [name for name, value in values.items() if not value]
    if missing:
        parser.error('Set required environment variables: ' + ', '.join(missing))

    root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(root / 'tools/voice'))
    from device import Device

    device = Device(args.out, port=args.port)
    try:
        for _ in range(3):
            device.lines()
        state = device.command('agent status', query=True)
        if state.get('device_id') != args.device_id:
            raise RuntimeError('Unexpected physical device; no configuration was changed')
        device.command('agent voice off')
        if args.init_context:
            device.command('agent context init', timeout=50)
        nonce = secrets.token_hex(8)
        config = {
            'ssid': args.ssid, 'password': values['WIFI_PASSWORD'],
            'deepseek_api_key': values['DEEPSEEK_API_KEY'],
            'user_id': 'local-user', 'session_id': 'p4-3.5',
            'epoch': int(time.time()), 'nonce': nonce,
        }
        device.send('@provision ' + json.dumps(config, separators=(',', ':')), secret=True)
        del config
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if any(line == '@provision_ok ' + nonce for line in device.lines()):
                break
        else:
            raise TimeoutError('Provision acknowledgement missing')
        # The stored voice belongs to the compatible standalone TTS route.
        # Natural mode uses AGENT_AUDIO_MODEL / AGENT_AUDIO_VOICE in firmware,
        # with the same DashScope key; do not store the realtime voice as TTS.
        device.command('agent voice configure ' + json.dumps({
            'provider': 'qianwen', 'api_key': values['DASHSCOPE_API_KEY'], 'voice': 'longanhuan_v3.6',
        }), secret=True)
        values.clear()
        device.command('agent voice mode natural')
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline:
            state = device.command('agent status', query=True, timeout=8)
            if state.get('wifi'):
                break
            time.sleep(1)
        if not state.get('wifi'):
            raise RuntimeError('Wi-Fi connection did not complete; voice remains off')
        device.command('agent voice on')
        voice, wake = wait_for_listening(device)
        state = device.command('agent status', query=True, timeout=8)
        if not state.get('wifi'):
            raise RuntimeError('Wi-Fi disconnected before provisioning completed')
        audio = device.command('agent audio status', query=True)
        result = {'passed': True, 'device': state, 'voice': voice, 'wake': wake, 'audio': audio}
        (args.out / 'status.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
        print(json.dumps(result, ensure_ascii=False))
    except Exception as error:
        # Replace an older success report even when this run fails. Do not write
        # arbitrary exception text, which might contain device/provider output.
        result = {'passed': False, 'error_type': type(error).__name__}
        (args.out / 'status.json').write_text(json.dumps(result, indent=2), encoding='utf8')
        print(json.dumps(result))
        raise
    finally:
        values.clear()
        device.save()
        device.close()


if __name__ == '__main__':
    main()
