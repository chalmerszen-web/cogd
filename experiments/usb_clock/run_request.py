"""Read a real MCU request, verify Flash unchanged, and restore the current app."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/clock-20261010/request-run'
sys.path.insert(0,str(ROOT/'tools'))
from serial_link import connect
from migrate_context import application_size

EXPECTED='我要变成一个时钟，24小时制，显示时、分，右下角小字显示秒。'

def save(name,value):
    (OUT/name).write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf8')

def rom(args,after='no-reset',no_stub=False):
    assert args[0] in ('read-flash','verify-flash','load-ram','read-mem')
    cmd=[sys.executable,'-m','esptool','--chip','esp32c3','--port','COM5','--baud','460800','--after',after]
    if no_stub:cmd.append('--no-stub')
    with (OUT/'esptool.log').open('a',encoding='utf8') as log:
        subprocess.run(cmd+[str(v) for v in args],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=150)

def main():
    OUT.mkdir(parents=True,exist_ok=False)
    report={'passed':False,'errors':[],'host_trigger':'GO\\n','expected_text':EXPECTED}
    before=None
    try:
        print('Backing up current Flash and verifying device...',flush=True)
        rom(['read-flash','0','0x400000',OUT/'flash-before.bin'])
        before=(OUT/'flash-before.bin').read_bytes();assert len(before)==4194304
        rom(['verify-flash','0',OUT/'flash-before.bin'])
        app=before[0x10000:0x190000];app=app[:application_size(app)]
        (OUT/'application-before.bin').write_bytes(app)
        report['original_application_sha256']=hashlib.sha256(app).hexdigest()
        image=ROOT/'artifacts/clock-20261010/request-build/request.bin'
        build=json.loads(image.with_name('build.json').read_text('utf8'))
        assert hashlib.sha256(image.read_bytes()).hexdigest()==build['sha256']
        print('Loading RAM sender; collecting device USB request...',flush=True)
        rom(['load-ram',image],no_stub=True)
        request=None;sent=False;pending=b'';deadline=time.monotonic()+15
        with connect('COM5') as link,(OUT/'usb-rx.bin').open('xb') as raw:
            while request is None and time.monotonic()<deadline:
                data=link.read(1024);raw.write(data);raw.flush();pending+=data
                while b'\n' in pending:
                    line,pending=pending.split(b'\n',1)
                    try:value=json.loads(line.decode('utf8'))
                    except (UnicodeDecodeError,ValueError):continue
                    if value.get('type')=='ready' and not sent:
                        link.write(b'GO\n');link.flush();sent=True
                    if value.get('type')=='codex_request':request=value
                    if value.get('type')=='trap':raise RuntimeError('MCU trap')
        assert sent and request is not None,'No complete USB request'
        assert request=={'type':'codex_request','id':'clock-20261010-01','text':EXPECTED},request
        save('request.json',request);report['request_exact_match']=True
        print(json.dumps(request,ensure_ascii=False),flush=True)
    except Exception as e:
        report['errors'].append(repr(e))
    finally:
        try:
            if before is not None:
                print('Verifying unchanged Flash after RAM execution...',flush=True)
                rom(['read-flash','0','0x400000',OUT/'flash-after.bin'])
                after=(OUT/'flash-after.bin').read_bytes()
                report['flash_unchanged']=before==after
                report['flash_sha256']=hashlib.sha256(before).hexdigest()
            rom(['read-mem','0x600c0010'],after='hard-reset',no_stub=True)
            time.sleep(3)
            subprocess.run([sys.executable,str(ROOT/'tools/usb_command.py'),'agent status',
                'agent wake status','--port','COM5','--timeout','8','--log',str(OUT/'restored.json')],
                check=True,stdout=subprocess.DEVNULL,timeout=25)
            state=json.loads((OUT/'restored.json').read_text('utf8'))
            status=next(json.loads(line) for line in state[0]['response'].splitlines() if line.startswith('{"firmware"'))
            report['restored_version']=status['version']
            assert status['version']=='0.12.0-rc2'
        except Exception as e:report['errors'].append('restore/verify: '+repr(e))
        report['passed']=bool(report.get('request_exact_match') and report.get('flash_unchanged')
            and report.get('restored_version')=='0.12.0-rc2' and not report['errors'])
        save('result.json',report)
        print(json.dumps(report,ensure_ascii=False,indent=2),flush=True)
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
