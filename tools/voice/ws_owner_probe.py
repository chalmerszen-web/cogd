"""One three-cycle device connection probe; no capture, generation or playback."""
import argparse
import hashlib
import json
from pathlib import Path
import time


def run(out, execute=False):
    out.mkdir(parents=True, exist_ok=False)
    report = dict(execute=execute, version='0.11.97-protocol-diag', cycles=3,
                  response_requests=0, audio_uploads=0, retries=0, complete=False,
                  script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    save = lambda: (out/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    (out/'executed-script.py').write_bytes(Path(__file__).read_bytes())
    save()
    if not execute:
        return report
    from device import Device
    d = Device(out)
    pending = False
    try:
        until = time.monotonic()+.5
        while time.monotonic()<until:
            d.lines()
        report['before'] = d.command('agent status', query=True)
        report['voice'] = d.command('agent voice status', query=True)
        report['context_before'] = d.command('agent context stats', query=True)
        if report['before']['version'] != report['version'] or report['before']['busy'] or report['voice']['voice_enabled']:
            raise RuntimeError('Expected idle diagnostic firmware with voice off')
        pending = True
        command = d.command('agent voice connections-probe', timeout=60)
        pending = False
        stages = []
        for line in command['lines']:
            try:
                value = json.loads(line)
            except ValueError:
                continue
            if isinstance(value, dict) and value.get('ws_probe') == 1:
                stages.append(value)
        report['stages'] = stages
        expected = [(0, 'before')]
        for cycle in range(1, 4):
            expected += [(cycle, 'asr'), (cycle, 'both'),
                         (cycle, 'asr_closed' if cycle % 2 else 'reply_closed'), (cycle, 'closed')]
        expected.append((0, 'expired'))
        if [(s['cycle'], s['stage']) for s in stages] != expected or any(s['error'] != 'ok' for s in stages):
            raise RuntimeError('Incomplete or failed connection probe')
        report['after'] = d.command('agent status', query=True)
        report['wake'] = d.command('agent wake status', query=True)
        report['context_after'] = d.command('agent context stats', query=True)
        if report['after']['busy'] or report['wake']['dma_lost']:
            raise RuntimeError('Device not idle or DMA loss')
        report['local_minimum'] = min(s['min'] for s in stages if s['stage'] != 'before')
        report['heap_gate_48k'] = report['local_minimum'] >= 49152
        report['complete'] = True
        report['limitation'] = 'Idle two-session resource probe only; no capture, output, extra worker or dialog-latency proof.'
    except Exception as error:
        report['error'] = type(error).__name__+': '+str(error)
        raise
    finally:
        if pending:
            # Never restart an uncertain request. Cancel it and retain the original log.
            try:
                d.command('agent cancel', timeout=12)
                report['cleanup'] = d.command('agent status', query=True, timeout=12)
            except Exception as error:
                report['cleanup_error'] = type(error).__name__+': '+str(error)
        d.save()
        d.close()
        save()
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    result = run(args.out, args.execute)
    print(json.dumps({k: result.get(k) for k in ('complete', 'local_minimum', 'heap_gate_48k', 'error')}))
