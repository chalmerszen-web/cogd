"""Bounded board signature/timing for a standalone frontend, live model unchanged."""
import json
import math
from pathlib import Path
import struct
import time
import zlib

from serial_test import Link

ROOT = Path(__file__).resolve().parents[2]


def expected_crc():
    estimate = [0] * 40
    random = 17
    checksum = 0
    for frame in range(256):
        values = []
        for band in range(40):
            random = (random * 1664525 + 1013904223) & 0xffffffff
            power = 0 if frame in (0, 2) else 0xffffffff if frame == 1 else random
            magnitude = math.isqrt(power)
            estimate[band] = (31 * estimate[band] + magnitude * 65536 + 16) // 32
            ratio = magnitude * 2 ** 28 // (estimate[band] + 65536)
            values.append(math.isqrt((ratio + 8192) * 16) - 362)
        checksum = zlib.crc32(struct.pack('<40h', *values), checksum)
    return checksum


def main():
    out = ROOT / 'artifacts/kws-phase5/pcen-prototype'
    assert not (out / 'board-check.json').exists()
    report = dict(complete=False, expected_crc=expected_crc(), checks=[])
    link = Link('COM5', out / 'board-serial.log')
    try:
        report['before'] = link.command('agent status')
        report['wake_before'] = link.command('agent wake status')
        report['context_before'] = link.command('agent context stats')
        assert report['wake_before']['model'] == 'xiaoyan_ds_tcn24_ef3'
        assert report['wake_before']['threshold'] == 750 and not report['wake_before']['enabled']
        link.command('agent kws pcen-check', expected_error='argument')
        link.command('agent kws begin')
        for repeat in range(2):
            started = time.monotonic()
            result = link.command('agent kws pcen-check')
            result['command_ms'] = (time.monotonic() - started) * 1000
            report['checks'].append(result)
            assert result['crc'] == report['expected_crc'] and result['frames'] == 256
            assert result['state_bytes'] == 160 and result['max_us'] < 1500
            assert result['command_ms'] < 500
            assert result['heap_after'] == result['heap_before']
        link.command('agent kws end')
        link.command('agent wake on')
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            state = link.command('agent wake status')
            if state['state'] == 'listening':
                break
            time.sleep(.1)
        else:
            raise RuntimeError('Original listener did not resume')
        assert state['model'] == 'xiaoyan_ds_tcn24_ef3' and state['error'] == 'ok'
        time.sleep(.2)
        link.off()
        report['profile'] = link.command('agent kws profile')
        report['context_after'] = link.command('agent context stats')
        assert report['context_after'] == report['context_before']
        report['after'] = link.command('agent status')
        report['complete'] = True
    except BaseException as error:
        report['error'] = repr(error)
        raise
    finally:
        try:
            link.command('agent kws end')
            link.command('agent wake off')
        finally:
            link.close()
            (out / 'board-check.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(dict(complete=True, checks=report['checks'], model='xiaoyan_ds_tcn24_ef3')))


if __name__ == '__main__':
    main()
