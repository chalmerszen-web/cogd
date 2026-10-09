"""Check text paths and bounded idle TLS cache reclamation after a successful TTS."""
import argparse
import json
from pathlib import Path
import time
from device import Device


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--idle-seconds',type=int,default=125)
    p.add_argument('--min-reclaimed',type=int,default=3000)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    assert 60<=a.idle_seconds<=180 and a.min_reclaimed>0
    d=Device(a.out);r={'complete':False,'samples':[]}
    def save():
        (a.out/'report.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
    try:
        d.command('agent voice off')
        r['stream']=d.command('agent chat 请只回复 STREAM_OK',timeout=45)
        r['plain']=d.command('agent chat --no-stream 请只回复 NONSTREAM_OK',timeout=45)
        assert any('STREAM_OK' in line for line in r['stream']['lines'])
        assert any('NONSTREAM_OK' in line for line in r['plain']['lines'])
        start=time.monotonic()
        while True:
            status=d.command('agent status',query=True)
            r['samples'].append({'elapsed_s':round(time.monotonic()-start,2),**status});save()
            if time.monotonic()-start>=a.idle_seconds:break
            time.sleep(min(15,a.idle_seconds-(time.monotonic()-start)))
        r['reclaimed_bytes']=r['samples'][-1]['free_heap']-r['samples'][0]['free_heap']
        assert r['reclaimed_bytes']>=a.min_reclaimed,'Expected the idle session cache to be reclaimed'
        assert all(not x['busy'] and x['wifi'] for x in r['samples'])
        r['context']=d.command('agent context stats',query=True)
        assert r['context']['history_budget']==204800
        d.command('agent voice on');time.sleep(2)
        r['voice']=d.command('agent voice status',query=True)
        r['wake']=d.command('agent wake status',query=True)
        r['status']=d.command('agent status',query=True)
        assert r['voice']['voice_enabled'] and r['wake']['state']=='listening'
        r['complete']=True
    finally:
        d.save();d.close();r['serial_released']=True;save()
    print(json.dumps({'complete':True,'reclaimed_bytes':r['reclaimed_bytes'],
                      'free_heap':r['status']['free_heap'],'min_heap':r['status']['min_heap']}),flush=True)


if __name__=='__main__':main()
