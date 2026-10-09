"""Guarded 0.6.3 rollback: checkpoint its supported budget before app-only flashing."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import subprocess
import sys
import time
from device import Device,ROOT
import install_latest


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--apply',action='store_true',help='Default only verifies the retained local package')
    p.add_argument('--port',default='COM5');a=p.parse_args()
    package=ROOT/'firmware/rollback/0.6.3-context'
    install_latest.PACKAGE=package
    manifest,_=install_latest.check_package()
    if manifest['firmware']!='0.6.3-context':raise ValueError('Unexpected rollback version')
    if not a.apply:
        print('0.6.3 package verified; no device changes. Use --apply for guarded rollback.');return
    out=ROOT/'artifacts/voice-cloud'/('rollback-'+datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S'))
    out.mkdir(parents=True,exist_ok=False)
    d=Device(out,a.port);report={'complete':False}
    def save():(out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    try:
        report['before']=d.command('agent status',query=True)
        if report['before']['busy']:raise RuntimeError('Device is busy; rollback refused')
        if report['before']['version'].startswith('0.8.'):
            d.command('agent voice off')
        else:d.command('agent wake off')
        report['context_before']=d.command('agent context stats',query=True);save()
        d.command('agent context budget 128')
        report['compatible_context']=d.command('agent context stats',query=True)
        if report['compatible_context']['history_budget']!=131072:raise RuntimeError('Compatible checkpoint was not committed')
        for key in ('events','last_local','partition_bytes'):
            if report['compatible_context'][key]!=report['context_before'][key]:raise RuntimeError('Context changed unexpectedly: '+key)
        save()
    finally:d.save();d.close()
    with (out/'flash.log').open('w',encoding='utf8') as log:
        subprocess.run([sys.executable,str(ROOT/'tools/kws/flash_guard.py'),'--port',a.port,
            '--phase','voice-cloud','--build',str(package)],stdout=log,stderr=subprocess.STDOUT,check=True)
    time.sleep(3)
    d=Device(out/'after',a.port)
    try:
        report['after']=d.command('agent status',query=True)
        report['context_after']=d.command('agent context stats',query=True)
        if report['after']['version']!='0.6.3-context' or not report['context_after']['ready']:
            raise RuntimeError('Rollback boot/context verification failed')
        for key in ('events','last_local','history_budget','partition_bytes'):
            if report['context_after'][key]!=report['compatible_context'][key]:raise RuntimeError('Rollback context mismatch: '+key)
        report['complete']=True;save()
    finally:d.save();d.close()
    print('Rollback verified; credentials/history preserved. Evidence: '+str(out))


if __name__=='__main__':main()
