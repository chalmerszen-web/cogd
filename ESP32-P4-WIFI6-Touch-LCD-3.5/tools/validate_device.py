"""Bounded reset, display-control, idle and negative-wake checks on COM3.

This checks telemetry, not human perception of the LCD or broad acoustic QA.
Leaves voice enabled and the clock displayed after a successful run.
"""
from pathlib import Path
import argparse,hashlib,json,re,sys,time
PROJECT=Path(__file__).resolve().parents[1];ROOT=PROJECT.parent
sys.path[:0]=[str(ROOT/'tools/voice'),str(ROOT/'tools')]
from device import Device
from wave_play import play
from conversation_test import leveled

p=argparse.ArgumentParser();p.add_argument('--port',default='COM3');p.add_argument('--out',type=Path,required=True)
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
d=Device(a.out,a.port);d.QUERY_FIELDS.update({'agent hardware':'board','agent display status':'ready'})
report={'complete':False,'resets':[],'negative_controls':[],'scope':'RTS hardware resets; fixed acoustic fixtures; no physical power-cycle or human/noise acceptance'}
panic=re.compile(r'Guru Meditation|assert failed|abort\(\)|watchdog|Stack protection fault',re.I)
def collect(seconds,allow_boot=False):
    end=time.monotonic()+seconds;lines=[]
    while time.monotonic()<end:
        for line in d.lines():
            lines.append(line)
            if panic.search(line) or (not allow_boot and 'ESP-ROM:' in line):raise RuntimeError('Unexpected runtime fault: '+line)
    return lines
def save():
    (a.out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');d.save()
try:
    report['hardware']=d.command('agent hardware',query=True,timeout=8)
    assert report['hardware']['cpu_mhz']==200,report['hardware']
    d.command('agent voice on',timeout=8)
    for index in range(3):
        d.link.rts=True;time.sleep(.15);d.link.rts=False
        boot=collect(12,True)
        assert sum('ESP-ROM:' in line for line in boot)==1,'Reset did not produce exactly one ROM banner'
        state=d.command('agent status',query=True,timeout=8)
        wake=d.command('agent wake status',query=True,timeout=8)
        assert state['device_id']=='esp-hi-e8f60aeb7a3f' and state['wifi'] and state['key_configured']
        assert wake['enabled'] and wake['state']=='listening',wake
        collect(12)
        report['resets'].append({'index':index+1,'system':state,'wake':wake,'result':'PASS'});save()
    report['display']=[]
    for command,mode in [('agent display set {"mode":"fill","foreground":63488}',2),('agent display off',0),
                         ('agent display set {"mode":"clock","foreground":65535,"background":0,"utc_offset_minutes":480}',1)]:
        d.command(command,timeout=8);collect(1)
        state=d.command('agent display status',query=True,timeout=8)
        assert state['mode']==mode and state['applied'] and state['ready'],state
        report['display'].append(state)
    manifest=ROOT/'artifacts/kws-phase6/replay-selection.jsonl'
    rows=[json.loads(x) for x in manifest.read_text(encoding='utf8').splitlines()]
    report['negative_manifest_sha256']=hashlib.sha256(manifest.read_bytes()).hexdigest()
    for row in (r for r in rows if r['label']==0):
        before=d.command('agent wake status',query=True,timeout=8)
        target=a.out/(row['clip_id']+'.wav');meta=leveled(ROOT/row['path'],target)
        playback=play(target,gain=.35);collect(3)
        after=d.command('agent wake status',query=True,timeout=8)
        passed=after['wakes']==before['wakes'] and after['state']=='listening'
        report['negative_controls'].append({'text':row['text'],'language':row['language'],'clip_id':row['clip_id'],
            'audio':meta,'playback':playback,'wake_delta':after['wakes']-before['wakes'],'passed':passed});save()
        if not passed:
            d.command('agent voice off',timeout=12);collect(2);d.command('agent voice on',timeout=8);collect(2)
    before=d.command('agent wake status',query=True,timeout=8);collect(30)
    after=d.command('agent wake status',query=True,timeout=8)
    report['idle_30s']={'before':before['wakes'],'after':after['wakes'],'passed':after['wakes']==before['wakes']}
    report['final']={'system':d.command('agent status',query=True,timeout=8),'wake':after,
                     'voice':d.command('agent voice status',query=True,timeout=8)}
    report['complete']=all(x['passed'] for x in report['negative_controls']) and report['idle_30s']['passed']
except Exception as e:
    report['error']=str(e);raise
finally:
    save();d.close()
    print(json.dumps({k:report[k] for k in ['complete','scope']},ensure_ascii=False))
