"""Bounded LCD/clock checks on COM5. SPI success does not verify visible pixels."""
import argparse
import json
from pathlib import Path
import time
from serial_link import connect, receive_until


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', default='COM5')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--version', default='0.6.2-repair')
    a = p.parse_args()
    report = {'steps': [], 'passed': False, 'visual_verified': False}
    try:
        with connect(a.port) as link:
            def cmd(text, query=False):
                link.reset_input_buffer()
                started = time.monotonic()
                link.write((text + '\n').encode())
                raw = receive_until(link, (b'}\r\n', b'@error') if query else (b'@ok', b'@error'), 10).decode('utf8', 'replace')
                report['steps'].append({'command': text, 'reply': raw, 'ms': round((time.monotonic()-started)*1000, 2)})
                if query:
                    return next(json.loads(line) for line in raw.splitlines() if line.startswith('{'))
                return raw

            def display():
                return cmd('agent display status', True)

            def settled(mode):
                deadline = time.monotonic() + 4
                while time.monotonic() < deadline:
                    state = display()
                    assert state['error'] == 'ok', state
                    if state['mode'] == mode and not state['pending'] and state['pins_owned'] == (mode != 'off'):
                        return state
                    time.sleep(.08)
                raise AssertionError(f'LCD did not settle: {state}')

            before = report['before'] = cmd('agent status', True)
            assert before['version'] == a.version and not before['busy']
            report['context_before'] = cmd('agent context stats', True)
            hw = report['hardware'] = cmd('agent hardware', True)
            assert (hw['display']['mosi'], hw['display']['sclk'], hw['display']['dc']) == (4, 5, 10)
            assert not hw['display']['controller_verified']
            light = cmd('agent light get', True)
            assert '@ok' in cmd('agent display set {"mode":"clock","utc_offset_minutes":480}')
            first = settled('clock')
            time.sleep(2.1)
            last = display()
            assert last['ready'] and last['frames'] >= first['frames']+2
            assert last['time_valid'] and last['time'] != first['time']
            expected = time.gmtime(time.time()+480*60)
            actual = time.strptime(last['time'], '%H:%M:%S')
            delta = (actual.tm_hour-expected.tm_hour)*3600 + (actual.tm_min-expected.tm_min)*60 + actual.tm_sec-expected.tm_sec
            assert min(abs(delta), abs(delta+86400), abs(delta-86400)) <= 2, delta
            for pin in (4, 5, 10):
                assert '@error busy' in cmd(f'agent gpio set {{"pin":{pin},"mode":"output","value":1}}')
            assert '@error busy' in cmd('agent control run {"steps":[{"op":"gpio_write","pin":10,"value":1}]}')
            assert '@ok' in cmd('agent light set 0 4 0')
            assert cmd('agent light get', True) == {'r': 0, 'g': 4, 'b': 0}
            assert '@error argument' in cmd('agent display set {"mode":"clock","utc_offset_minutes":900}')
            assert '@error argument' in cmd('agent display set {"mode":"clock","foreground":0}')
            assert display()['mode'] == 'clock'
            began = time.monotonic()
            assert '@ok' in cmd('agent cancel')
            settled('off')
            report['cancel_ms'] = round((time.monotonic()-began)*1000, 2)
            assert report['cancel_ms'] < 500
            assert '@ok' in cmd('agent gpio get {"pin":10}')
            # Repeated open/close checks catch resource leaks without another long campaign.
            heaps = []
            for i in range(5):
                assert '@ok' in cmd('agent display set {"mode":"fill","foreground":31}')
                settled('fill')
                assert '@ok' in cmd('agent display off')
                settled('off')
                heaps.append(cmd('agent status', True)['free_heap'])
            report['off_heaps'] = heaps
            assert max(heaps[1:])-min(heaps[1:]) <= 1024
            assert '@ok' in cmd(f"agent light set {light['r']} {light['g']} {light['b']}")
            report['after'] = cmd('agent status', True)
            report['context_after'] = cmd('agent context stats', True)
            assert report['after']['control_stack'] >= 256
            assert report['after']['min_heap'] >= 48*1024
            assert report['context_before'] == report['context_after']
            report['passed'] = True
    finally:
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps({k: v for k, v in report.items() if k != 'steps'}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
