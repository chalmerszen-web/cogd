"""Bounded RAM-only LCD probe; full Flash readback and automatic restoration."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/clock-20261010/lcd-probe-run'
sys.path.insert(0,str(ROOT/'tools'))
from serial_link import connect


def rom(args,after='no-reset',no_stub=False):
    assert args[0] in ('read-flash','verify-flash','load-ram','read-mem')
    command=[sys.executable,'-m','esptool','--chip','esp32c3','--port','COM5',
             '--baud','460800','--after',after]
    if no_stub:command.append('--no-stub')
    with (OUT/'esptool.log').open('a',encoding='utf8') as log:
        subprocess.run(command+list(map(str,args)),check=True,stdout=log,
                       stderr=subprocess.STDOUT,timeout=150)


def main():
    OUT.mkdir(parents=True,exist_ok=False)
    before=None
    report={'errors':[],'visual_verified':False,'events':[]}
    try:
        print('Backing up and verifying full Flash before RAM-only probe.',flush=True)
        rom(['read-flash','0','0x400000',OUT/'flash-before.bin'])
        before=(OUT/'flash-before.bin').read_bytes();assert len(before)==4194304
        rom(['verify-flash','0',OUT/'flash-before.bin'])
        image=ROOT/'artifacts/clock-20261010/lcd-probe-build/probe.bin'
        build=json.loads(image.with_name('build.json').read_text('utf8'))
        assert hashlib.sha256(image.read_bytes()).hexdigest()==build['sha256']
        rom(['load-ram',image],no_stub=True)
        pending=b'';sent=False;deadline=time.monotonic()+100;hold_until=None
        with connect('COM5') as link,(OUT/'usb-rx.bin').open('xb') as raw:
            while time.monotonic()<deadline and (hold_until is None or time.monotonic()<hold_until):
                data=link.read(1024);raw.write(data);raw.flush();pending+=data
                while b'\n' in pending:
                    line,pending=pending.split(b'\n',1)
                    try:value=json.loads(line.decode('utf8'))
                    except (ValueError,UnicodeDecodeError):continue
                    if value.get('type')=='hold':continue
                    report['events'].append(value);print(json.dumps(value),flush=True)
                    if value.get('type')=='ready' and not sent:
                        link.write(b'G\n');link.flush();sent=True
                    if value.get('type')=='drawn' and value.get('pattern')=='bands':
                        hold_until=time.monotonic()+60
                    if value.get('type')=='trap':raise RuntimeError('RAM program trapped')
        assert any(e.get('pattern')=='bands' for e in report['events']),'No completed pattern'
        pads=next(e for e in report['events'] if e.get('type')=='pads')
        report['pad_readback_passed']=pads['low']=='0x00000000' and pads['high']=='0x00000430'
    except Exception as error:
        report['errors'].append(repr(error))
    finally:
        try:
            if before is not None:
                print('Checking Flash unchanged, then restarting installed clock.',flush=True)
                rom(['read-flash','0','0x400000',OUT/'flash-after.bin'])
                report['flash_unchanged']=before==(OUT/'flash-after.bin').read_bytes()
                report['flash_sha256']=hashlib.sha256(before).hexdigest()
            rom(['read-mem','0x600c0010'],after='hard-reset',no_stub=True)
            time.sleep(3)
            subprocess.run([sys.executable,str(ROOT/'tools/usb_command.py'),'agent status',
                'agent display status','--timeout','10','--log',str(OUT/'restored.json')],
                check=True,stdout=subprocess.DEVNULL,timeout=30)
            states=json.loads((OUT/'restored.json').read_text('utf8'))
            state=next(json.loads(line) for line in states[0]['response'].splitlines()
                       if line.startswith('{"firmware"'))
            report['restored_version']=state['version']
            assert state['version']=='0.12.1-clock'
        except Exception as error:report['errors'].append('restore/verify: '+repr(error))
        (OUT/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
        print(json.dumps(report,indent=2),flush=True)
    return 1 if report['errors'] or not report.get('flash_unchanged') else 0


if __name__=='__main__':raise SystemExit(main())
