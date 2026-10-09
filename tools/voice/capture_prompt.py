"""One fixed-duration physical input recording, independent of cloud endpoints."""
import argparse
import json
from pathlib import Path
import time
from device import Device
from record_speaker import Recorder
from wave_play import play


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    device = Device(args.out)
    report = {'complete': False, 'source': str(args.source), 'cloud_upload': False}
    try:
        device.command('agent voice off')
        with Recorder(args.out / 'speaker.wav', seconds=15):
            device.command('agent audio capture 6000')
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                status = device.command('agent audio status', query=True)
                if status['recording'] and status['capture_stage'] == 2:
                    break
            else:
                raise TimeoutError('Capture did not start')
            report['playback'] = play(args.source, gain=.35)
            deadline = time.monotonic() + 9
            while time.monotonic() < deadline:
                status = device.command('agent audio status', query=True)
                if not status['recording']:
                    break
            report['state'] = status
            report['complete'] = status['clip_ready'] and status['clip_ms'] == 6000 and status['capture_error'] == 'ok'
    finally:
        device.save()
        device.close()
        (args.out / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    if not report['complete']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
