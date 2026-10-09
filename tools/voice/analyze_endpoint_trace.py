"""Validate board metadata and replay the exact C endpoint offline. No USB/cloud.

Enabled trace collection adds a bounded post-capture USB delay. These captures
can diagnose classification but must not be used for response-speed acceptance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import struct
import zlib


ROOT = Path(__file__).resolve().parents[2]


def wsl_path(path):
    path = Path(path).resolve()
    if path.drive:
        return '/mnt/' + path.drive[0].lower() + '/' + '/'.join(path.parts[1:])
    return str(path)


def read_replay(raw, expected_frames):
    frames = [json.loads(line) for line in raw.splitlines()]
    if len(frames) != expected_frames or any(f.get('ms') != (i+1)*20 for i,f in enumerate(frames)):
        raise ValueError('Incomplete or out-of-order C replay output')
    return frames


def decode(lines):
    traces = {}
    for line in lines:
        if not line.startswith('@endpoint '):
            continue
        event = json.loads(line[len('@endpoint '):])
        ident = event['id']
        if not isinstance(ident, int) or ident <= 0:
            raise ValueError('Invalid trace id')
        if event['kind'] == 'begin':
            if ident in traces or not 1 <= event['frames'] <= 500:
                raise ValueError('Duplicate/invalid trace header')
            if not 0 <= event['noise'] <= 32768 or not 400 <= event['end_ms'] <= 2000:
                raise ValueError('Invalid endpoint configuration')
            if not isinstance(event.get('transcribed_ms',0),int) or not 0 <= event.get('transcribed_ms',0) < 4000 or event.get('transcribed_ms',0)%20:
                raise ValueError('Invalid ASR admission source time')
            if not isinstance(event.get('shadow',False),bool):raise ValueError('Invalid shadow marker')
            traces[ident] = {'header': event, 'data': bytearray(), 'notices': bytearray()}
        elif ident not in traces or 'end' in traces[ident]:
            raise ValueError('Unframed or already sealed metadata')
        elif event['kind'] == 'frames':
            trace = traces[ident]
            data = bytes.fromhex(event['hex'])
            if (not data or len(data) > 96 or len(data) % 6 or
                    event['offset'] * 6 != len(trace['data']) or
                    len(trace['data']) + len(data) > trace['header']['frames'] * 6):
                raise ValueError('Missing, duplicated or oversized metadata')
            trace['data'].extend(data)
        elif event['kind'] == 'notices':
            trace = traces[ident]
            data = bytes.fromhex(event['hex'])
            if (not data or len(data) > 64 or len(data) % 8 or
                    event['offset'] * 8 != len(trace['notices']) or
                    len(trace['notices']) + len(data) > (3200 if trace['header'].get('shadow') else 512)):
                raise ValueError('Missing, duplicated or oversized notices')
            trace['notices'].extend(data)
        elif event['kind'] == 'end':
            trace = traces[ident]
            if len(trace['data']) != trace['header']['frames'] * 6:
                raise ValueError('Incomplete trace')
            if f"{zlib.crc32(trace['data']):08x}" != event['crc32']:
                raise ValueError('Trace CRC mismatch')
            if trace['notices'] or 'notice_count' in event:
                size = len(trace['notices'])
                if (not size or event.get('notice_count') != size // 8 or
                        event.get('notice_overflow') is not False or
                        event.get('notice_crc32') != f"{zlib.crc32(trace['notices']):08x}"):
                    raise ValueError('Incomplete, overflowed or corrupt notices')
                rows = list(struct.iter_unpack('<II', trace['notices']))
                if (rows[0][0] != 0 or any(ms % 20 or ms >= trace['header']['frames'] * 20 for ms, _ in rows) or
                        any(b[0] <= a[0] or b[1] == a[1] for a, b in zip(rows, rows[1:]))):
                    raise ValueError('Invalid notice source clock or transitions')
            trace['end'] = event
        else:
            raise ValueError('Unknown metadata kind')
    return traces


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--supported',action='store_true',help='Replay the two-strong/four-weak continuation policy')
    parser.add_argument('--output-name',default='endpoint-analysis',help='New output directory name; existing evidence is never overwritten')
    parser.add_argument('--replay', type=Path,
                        default=ROOT/'artifacts/voice-fast/host-handoff-agent/replay_endpoint')
    args = parser.parse_args()
    if Path(args.output_name).name != args.output_name or args.output_name in ('', '.', '..'):
        parser.error('--output-name must be a single directory name')
    out = args.directory / args.output_name
    out.mkdir(exist_ok=False)
    report_bytes = (args.directory/'report.json').read_bytes()
    report = json.loads(report_bytes)
    raw = (args.directory/'serial.log').read_bytes()
    traces = decode(raw.decode('utf8').splitlines())
    summary = dict(firmware=report['before']['version'],
                   report_sha256=hashlib.sha256(report_bytes).hexdigest(),
                   serial_sha256=hashlib.sha256(raw).hexdigest(),
                   replay_sha256=hashlib.sha256(args.replay.read_bytes()).hexdigest(),
                   script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                   latency_acceptance_eligible=False,supported=args.supported, traces=[])
    for ident, trace in traces.items():
        header = trace['header']
        row = dict(header=header, complete='end' in trace, round=None)
        summary['traces'].append(row)
        for trial in report['trials']:
            starts = [e['time_ms'] for e in trial['events'] if e['stage'] == 'capture_begin']
            ends = [e['time_ms'] for e in trial['events'] if e['stage'] in ('vad_end', 'error')]
            if starts and ends and min(starts) <= header['time_ms'] <= max(ends):
                if row['round'] is not None:
                    raise ValueError('Ambiguous turn assignment')
                row.update(round=trial['round'], asr=trial.get('asr'))
        if not row['complete']:
            row['error'] = 'USB trace incomplete; no replay inference'
            continue
        source = out/f'trace-{ident:02d}.bin'
        source.write_bytes(trace['data'])
        row['crc32'] = trace['end']['crc32']
        row['diagnostic_ms'] = trace['end']['elapsed_ms']
        replay_path = out/f'trace-{ident:02d}.jsonl'
        notice_args = []
        if trace['notices']:
            if not args.supported:raise ValueError('Exact notice replay requires --supported')
            notice_path = out/f'trace-{ident:02d}-notices.bin'
            notice_path.write_bytes(trace['notices'])
            notice_args = ['--notices', wsl_path(notice_path)]
            if header.get('shadow'):notice_args+=['--shadow']
            row['notices'] = [{'before_ms': ms, 'notice': notice}
                             for ms, notice in struct.iter_unpack('<II', trace['notices'])]
        # WSL's Windows stdout pipe can report success yet truncate output.
        # Write inside WSL, then validate every expected frame from the file.
        if replay_path.exists():raise FileExistsError(replay_path)
        subprocess.run(['wsl', '-d', 'Ubuntu', '--', wsl_path(args.replay),
                                 wsl_path(source), str(header['noise']), str(header['end_ms']),
                                 '--output', wsl_path(replay_path)]+
                                (['--supported'] if args.supported else [])+
                                (notice_args or (['--transcribed',str(header['transcribed_ms'])] if header.get('transcribed_ms') else [])),
                                check=True, capture_output=True, timeout=10)
        frames = read_replay(replay_path.read_bytes(), header['frames'])
        final = frames[-1]
        keys = ('state', 'elapsed_ms', 'speech_ms', 'quiet_ms', 'resume_frames')
        if 'transcribed_ms' in header:keys += ('transcribed_ms',)
        row['parity'] = {k: final[k] == header[k] for k in keys}
        if trace['notices']:
            row['parity'].update({k: final[k] == trace['end'][k] for k in
                ('held_ms', 'dense_ms', 'asr_notice', 'asr_floor_ms', 'local_onset')})
        row['parity_passed'] = all(row['parity'].values())
        positive = [f for f in frames if f['positive'] and f['applied']]
        row['positive_frames'] = len(positive)
        row['last_positive_ms'] = positive[-1]['ms'] if positive else None
        row['threshold'] = max(240, header['noise']*2)
        runs = []
        for frame in frames:
            if frame['positive'] and frame['applied']:
                if runs and runs[-1][1] == frame['ms']-20:
                    runs[-1][1] = frame['ms']
                else:
                    runs.append([frame['ms']-20, frame['ms']])
        row['positive_runs_ms'] = runs
    summary['complete'] = (len(traces) == 3 and all(
        t['complete'] and t.get('parity_passed') and t['round'] is not None for t in summary['traces']))
    (out/'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps(summary, ensure_ascii=False))
    if not summary['complete']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
