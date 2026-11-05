"""Bounded final idle check; leave the identified board ready for human use."""
import argparse,json,re,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/voice'),str(ROOT/'tools')]
from device import Device

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True)
    p.add_argument('--port',default='COM3');p.add_argument('--firmware',required=True)
    p.add_argument('--idle-seconds',type=int,default=30)
    p.add_argument('--startup-settle-seconds',type=int,default=12,
                   help='Drain startup logs before querying JSON after an esptool reset')
    a=p.parse_args()
    assert 10<=a.idle_seconds<=60 and 0<=a.startup_settle_seconds<=30
    a.out.mkdir(parents=True,exist_ok=False);d=Device(a.out,a.port)
    d.QUERY_FIELDS['agent hardware']='board';r={'complete':False}
    def save():
        (a.out/'status.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8');d.save()
    try:
        # IDF boot/network messages can interleave a JSON status response.
        # This is preflight only; the separate idle acceptance follows below.
        r['startup_settle_seconds']=a.startup_settle_seconds
        settle=time.monotonic()+a.startup_settle_seconds
        while time.monotonic()<settle:
            for line in d.lines():
                assert not re.search(r'Guru Meditation|assert failed|watchdog|abort\(',line,re.I),line
        d.command('agent voice on',timeout=8);d.command('agent wake on',timeout=8)
        end=time.monotonic()+25
        while time.monotonic()<end:
            system=d.command('agent status',query=True,timeout=5)
            before=d.command('agent wake status',query=True,timeout=5)
            if system['wifi'] and before['state']=='listening':break
            time.sleep(.2)
        assert system['device_id']=='esp-hi-e8f60aeb7a3f' and system['version']==a.firmware
        assert system['wifi'] and before['state']=='listening'
        end=time.monotonic()+a.idle_seconds
        while time.monotonic()<end:
            for line in d.lines():
                assert not re.search(r'Guru Meditation|assert failed|ESP-ROM:|watchdog|abort\(',line,re.I),line
        r.update(system=d.command('agent status',query=True,timeout=5),
                 hardware=d.command('agent hardware',query=True,timeout=5),
                 wake=d.command('agent wake status',query=True,timeout=5),
                 voice=d.command('agent voice status',query=True,timeout=5),
                 profile=d.command('agent kws profile',query=True,timeout=5))
        r['idle']={'seconds':a.idle_seconds,'wakes_before':before['wakes'],'wakes_after':r['wake']['wakes']}
        assert r['hardware']['cpu_mhz']==200 and r['voice']['voice_enabled']
        assert r['wake']['state']=='listening' and r['wake']['wakes']==before['wakes']
        assert r['wake']['dma_lost']==before['dma_lost']
        r['complete']=True
    except Exception as error:r['error']=str(error);raise
    finally:save();d.close()
    print(json.dumps({'complete':True,'idle_seconds':a.idle_seconds,'model_heap':r['wake']['model_heap'],
                      'workspace':r['profile']['workspace'],'cpu_mhz':r['hardware']['cpu_mhz']}))

if __name__=='__main__':main()
