"""Run native RAM firmware, compare full Flash, and recover the original app.

No write-flash, erase, credential, signing, encryption or eFuse operation.
Each run has an exclusive output directory so previous evidence is retained.
"""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'artifacts/native-led-20261009'
sys.path.insert(0, str(ROOT / 'tools/voice'))
sys.path.insert(0, str(ROOT / 'tools'))
from device import Device
from serial_link import connect


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def esptool(out, operation, *, after='no-reset', no_stub=False):
    allowed = {'read-flash', 'verify-flash', 'load-ram', 'read-mem'}
    if operation[0] not in allowed:
        raise ValueError('Only backup, verification, RAM load and reset operations allowed')
    command = [sys.executable, '-m', 'esptool', '--chip', 'esp32c3', '--port', 'COM5',
               '--baud', '460800', '--after', after]
    if no_stub: command.append('--no-stub')
    command += [str(x) for x in operation]
    with (out / 'esptool.log').open('a', encoding='utf-8') as log:
        log.write('\n' + subprocess.list2cmdline(command) + '\n')
        log.flush()
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=150)


def observe(out, events):
    link = connect('COM5')
    ready = False
    started = time.monotonic()
    pending = b''
    try:
        with (out / 'native-serial.log').open('wb') as log:
            while time.monotonic() - started < 80:
                data = link.read(1024)
                if data:
                    log.write(data)
                    log.flush()
                    pending += data
                while b'\n' in pending:
                    line, pending = pending.split(b'\n', 1)
                    try: event = json.loads(line.decode('utf-8'))
                    except (ValueError, UnicodeDecodeError): continue
                    if not isinstance(event, dict) or 'event' not in event: continue
                    event['host_seconds'] = time.monotonic() - started
                    events.append(event)
                    save(out / 'events.json', events)
                    print(json.dumps(event), flush=True)
                    if event['event'] in ('error', 'trap'):
                        raise RuntimeError('Native payload reported: ' + json.dumps(event))
                    if event['event'] == 'ready' and not ready:
                        link.write(b'GO\n')
                        link.flush()
                        ready = True
                    if event['event'] == 'done': return
                if not ready and time.monotonic() - started > 15:
                    raise TimeoutError('No native READY handshake after 15 seconds')
            raise TimeoutError('Native test did not finish within 80 seconds')
    finally:
        link.close()


def validate(events):
    steps = [e for e in events if e['event'] == 'step']
    done = [e for e in events if e['event'] == 'done']
    assert len(steps) == 20, f'Expected 20 colored intervals, got {len(steps)}'
    assert len(done) == 1 and done[0]['off'] is True
    names = ['red', 'green', 'blue']
    for i, event in enumerate(steps):
        assert event['step'] == i and event['color'] == names[i % 3]
        assert event['frames'] == i + 2
    assert done[0]['frames'] == 22
    ticks = [e['ticks'] for e in steps] + [done[0]['ticks']]
    periods = [(b - a) / 16000000 for a, b in zip(ticks, ticks[1:])]
    assert all(abs(period - 3) < 0.002 for period in periods), periods
    total = done[0]['visible_ticks'] / 16000000
    assert abs(total - 60) < 0.002, total
    host_total = done[0]['host_seconds'] - steps[0]['host_seconds']
    assert abs(host_total - 60) < 1.0, host_total
    return {'colored_intervals': 20, 'rmt_completed_frames': 22,
            'interval_min_seconds': min(periods), 'interval_max_seconds': max(periods),
            'first_color_to_off_seconds': total, 'host_elapsed_seconds': host_total,
            'optical_observation': 'NOT_OBSERVED_BY_AGENT',
            'timing_measurement': 'SYSTIMER snapshots after hardware RMT TX_END'}


def restore_status(out):
    # This read-only operation also requests a normal hardware reset on exit.
    esptool(out, ['read-mem', '0x600c0010'], no_stub=True, after='hard-reset')
    time.sleep(3)
    device = Device(out / 'restored')
    try:
        status = {}
        for command in ['agent status', 'agent voice status',
                        'agent audio status', 'agent light get']:
            status[command] = device.command(command, query=True, timeout=12)
        save(out / 'restored/state.json', status)
        return status
    finally:
        device.save()
        device.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--run-name', required=True)
    args = parser.parse_args()
    if not args.run_name.startswith('run-') or not all(
            c.isalnum() or c == '-' for c in args.run_name):
        raise SystemExit('Use a simple run-* name')
    out = BASE / args.run_name
    original = json.loads((BASE / 'firmware-before.json').read_text(encoding='utf-8'))
    image = BASE / 'build/native_led.bin'
    build = json.loads((BASE / 'build/build-report.json').read_text(encoding='utf-8'))
    assert sha(image.read_bytes()) == build['image_sha256']
    assert sha((BASE / 'flash-before.bin').read_bytes()) == original['full_flash_sha256']
    out.mkdir(exist_ok=False)
    shutil.copytree(BASE / 'build', out / 'build')
    (out / 'source').mkdir()
    for name in ['start.S', 'native_led.c', 'ram.ld', 'build.py', 'run_test.py']:
        shutil.copy2(Path(__file__).resolve().parent / name, out / 'source' / name)
    report = {'started_at': datetime.now(timezone(timedelta(hours=8))).isoformat(),
              'original_version': original['version'], 'image_sha256': build['image_sha256'],
              'method': 'ROM load-ram, no Flash write', 'errors': []}
    events = []
    hardware_started = False
    before = None
    try:
        hardware_started = True
        print('Capturing immediate pre-RAM Flash snapshot...', flush=True)
        pre = out / 'flash-before-ram.bin'
        esptool(out, ['read-flash', '0', '0x400000', pre])
        esptool(out, ['verify-flash', '0', pre])
        before = pre.read_bytes()
        assert len(before) == 4194304
        assert sha(before[0x10000:0x10000 + original['application_bytes']]) == original['application_sha256']
        report['flash_before_sha256'] = sha(before)
        print('Loading native RAM image; waiting for READY then starting 60 seconds...', flush=True)
        esptool(out, ['load-ram', image], no_stub=True)
        observe(out, events)
        report['native_execution'] = validate(events)
        report['native_pass'] = True
    except Exception as error:
        report['native_pass'] = False
        report['errors'].append(repr(error))
        print('Execution/validation issue: ' + repr(error), flush=True)
    finally:
        save(out / 'events.json', events)
        if hardware_started:
            if before is not None:
                try:
                    print('Reading Flash after RAM execution for byte-for-byte comparison...', flush=True)
                    post = out / 'flash-after-ram.bin'
                    esptool(out, ['read-flash', '0', '0x400000', post])
                    after = post.read_bytes()
                    report['flash_after_sha256'] = sha(after)
                    report['flash_unchanged'] = before == after
                    report['application_unchanged'] = sha(
                        after[0x10000:0x10000 + original['application_bytes']]) == original['application_sha256']
                    if not report['flash_unchanged']:
                        report['errors'].append('Full Flash changed between pre/post RAM snapshots')
                except Exception as error:
                    report['errors'].append('Post-Flash verification: ' + repr(error))
            try:
                print('Resetting to the original firmware...', flush=True)
                status = restore_status(out)
                report['restored_version'] = status['agent status']['version']
                report['original_firmware_running'] = report['restored_version'] == original['version']
                report['restored_voice_enabled'] = status['agent voice status']['voice_enabled']
                report['restored_voice_stage'] = status['agent voice status']['stage']
                report['restored_light'] = status['agent light get']
                if not report['original_firmware_running']:
                    report['errors'].append('Unexpected firmware version after reset')
                if report['restored_voice_enabled'] != original['original_voice_enabled']:
                    report['errors'].append('Voice-enabled state differs after reset')
                if report['restored_light'] != original['original_light']:
                    report['errors'].append('Light state differs after reset')
            except Exception as error:
                report['errors'].append('Firmware recovery: ' + repr(error))
        report['passed'] = bool(report.get('native_pass') and report.get('flash_unchanged')
                                and report.get('original_firmware_running') and not report['errors'])
        save(out / 'result.json', report)
        print(json.dumps(report, ensure_ascii=False, indent=2), flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
