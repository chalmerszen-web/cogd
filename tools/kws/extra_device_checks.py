"""Probe session failure paths and recording/playback API regression."""
import argparse
import json
from pathlib import Path
import sys
import time
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/kws'))
from serial_test import Link

def main():
    p=argparse.ArgumentParser();p.add_argument('--port',default='COM5');p.add_argument('--capture',action='store_true');p.add_argument('--out',default='artifacts/kws-phase1')
    args=p.parse_args();out=ROOT/args.out
    link=Link(args.port,out/'extra-device.log');report=dict(complete=False,started=time.time())
    try:
        report['before']=link.command('agent status');link.off()
        report['reopen_cycles']=[]
        for cycle in range(20):
            link.command('agent wake on');deadline=time.monotonic()+8
            while time.monotonic()<deadline:
                wake=link.command('agent wake status')
                if wake['error']!='ok':raise AssertionError(wake)
                if wake['state']=='listening':break
                time.sleep(.1)
            else:raise AssertionError('Repeated reopen did not reach listening')
            report['reopen_cycles'].append(dict(cycle=cycle,wake=wake,status=link.command('agent status')))
            link.off()
            if cycle%4==0:link.command('agent kws begin');link.command('agent kws end')
        print('20 aligned reopen cycles passed',flush=True)
        link.command('agent kws begin')
        link.command('agent kws frame 0 0 ff',expected_error='argument')
        link.command('agent audio capture 2000',expected_error='busy')
        start=time.monotonic()
        while time.monotonic()-start<33:
            line=link.link.readline().decode('utf8','replace').strip()
            if '@error timeout' in line: break
        else:raise AssertionError('Session idle expiry not observed')
        report['timeout_seconds']=time.monotonic()-start
        assert 29<=report['timeout_seconds']<33
        link.command('agent kws begin');link.command('agent kws end')
        report['timeout_released']=True
        report['audio_before']=link.command('agent audio status')
        if args.capture:
            link.command('agent audio capture 2000')
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                audio=link.command('agent audio status')
                if not audio['recording'] and audio['clip_ready'] and audio['clip_ms']<=2100:
                    report['captured']=audio;break
                time.sleep(.25)
            else:raise AssertionError('Capture did not finish')
            assert audio['capture_error']=='ok' and audio['clip_ms']>=1800
            link.command('agent audio volume 40')
            link.command('agent audio replay')
            deadline=time.monotonic()+10
            while time.monotonic()<deadline:
                audio=link.command('agent audio status')
                if not audio['playing']:
                    assert audio['play_error']=='ok';report['replayed']=audio;break
                time.sleep(.25)
            else:raise AssertionError('Replay did not finish')
        report['after']=link.command('agent status');report['complete']=True
    finally:
        link.command('agent kws end');link.command('agent audio stop')
        if 'audio_before' in report:link.command('agent audio volume '+str(report['audio_before']['volume']))
        link.close();(out/'extra-device-checks.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
