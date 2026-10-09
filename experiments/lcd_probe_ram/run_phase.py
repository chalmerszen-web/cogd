"""Eight bounded RAM LCD byte-alignment trials with fresh camera evidence."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
import run as base

ROOT=base.ROOT
OUT=ROOT/'artifacts/clock-20261010/lcd-phase-run'
BUILD=ROOT/'artifacts/clock-20261010/lcd-phase-build'
CAMERA=ROOT/'artifacts/clock-20261010/camera/live.jpg'

def main():
    OUT.mkdir(parents=True,exist_ok=False);base.OUT=OUT
    before=None
    report={'errors':[],'visual_verified':False,'events':[],'photos':[]}
    try:
        print('Backing up and verifying full Flash before RAM phase probe.',flush=True)
        base.rom(['read-flash','0','0x400000',OUT/'flash-before.bin'])
        before=(OUT/'flash-before.bin').read_bytes();assert len(before)==4194304
        base.rom(['verify-flash','0',OUT/'flash-before.bin'])
        image=BUILD/'probe.bin';build=json.loads((BUILD/'build.json').read_text('utf8'))
        assert hashlib.sha256(image.read_bytes()).hexdigest()==build['sha256']
        base.rom(['load-ram',image],no_stub=True)
        pending=b'';sent=False;deadline=time.monotonic()+100
        with base.connect('COM5') as link,(OUT/'usb-rx.bin').open('xb') as raw:
            while time.monotonic()<deadline:
                data=link.read(1024);raw.write(data);raw.flush();pending+=data
                while b'\n' in pending:
                    line,pending=pending.split(b'\n',1)
                    try:value=json.loads(line.decode('utf8'))
                    except (ValueError,UnicodeDecodeError):continue
                    value['received_at']=datetime.now(timezone.utc).isoformat()
                    report['events'].append(value);print(json.dumps(value),flush=True)
                    if value.get('type')=='ready' and not sent:
                        link.write(b'G\n');link.flush();sent=True
                    if value.get('type')=='trap':raise RuntimeError('RAM program trapped')
                    if value.get('type')=='drawn':
                        began=time.time();time.sleep(3)
                        assert CAMERA.stat().st_mtime>began,'Camera frame is stale'
                        dest=OUT/('phase-'+str(value['phase'])+'.jpg')
                        shutil.copyfile(CAMERA,dest)
                        report['photos'].append({'phase':value['phase'],'file':dest.name,
                            'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()})
                        if value['phase']==7:deadline=0;break
                        link.write(b'N\n');link.flush()
        assert len(report['photos'])==8,'Missing phase results'
    except Exception as error:report['errors'].append(repr(error))
    finally:
        try:
            if before is not None:
                print('Verifying unchanged Flash and restoring installed app.',flush=True)
                base.rom(['read-flash','0','0x400000',OUT/'flash-after.bin'])
                report['flash_unchanged']=before==(OUT/'flash-after.bin').read_bytes()
                report['flash_sha256']=hashlib.sha256(before).hexdigest()
            base.rom(['read-mem','0x600c0010'],after='hard-reset',no_stub=True)
            time.sleep(3)
            subprocess.run([sys.executable,str(ROOT/'tools/usb_command.py'),'agent status',
                'agent display status','--timeout','10','--log',str(OUT/'restored.json')],
                check=True,stdout=subprocess.DEVNULL,timeout=30)
            states=json.loads((OUT/'restored.json').read_text('utf8'))
            state=next(json.loads(line) for line in states[0]['response'].splitlines()
                       if line.startswith('{"firmware"'))
            report['restored_version']=state['version'];assert state['version']=='0.12.2-clock'
        except Exception as error:report['errors'].append('restore/verify: '+repr(error))
        (OUT/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
        print(json.dumps(report,indent=2),flush=True)
    return 1 if report['errors'] or not report.get('flash_unchanged') else 0

if __name__=='__main__':raise SystemExit(main())
