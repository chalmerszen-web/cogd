"""Real-device cancel during idle listening, wake capture and TTS; verify rearm/off."""
import argparse
import json
from pathlib import Path
import time
from device import Device,ROOT
from wave_play import play


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    d=Device(a.out);r={'complete':False}
    def poll(command,predicate,seconds=15):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            value=d.command(command,query=True)
            if predicate(value):return value
            time.sleep(.1)
        raise TimeoutError(command)
    def listening():
        wake=poll('agent wake status',lambda x:x['state']=='listening')
        voice=d.command('agent voice status',query=True)
        assert voice['voice_enabled'] and voice['stage']=='listening'
        return {'wake':wake,'voice':voice}
    def cancel_job():
        start=time.monotonic();ack=d.command('agent cancel')
        acknowledged_ms=round((time.monotonic()-start)*1000)
        end=time.monotonic()+15;cancelled=ack.get('cancelled_job',False)
        while not cancelled and time.monotonic()<end:
            for line in d.lines():
                if line=='@error cancelled':cancelled=True
        assert cancelled,'Cancelled worker did not finish'
        return acknowledged_ms
    try:
        d.command('agent voice off');d.command('agent voice on');listening()
        r['before']=d.command('agent context stats',query=True)
        start=time.monotonic();d.command('agent cancel');r['idle']=listening()
        r['idle_rearm_ms']=round((time.monotonic()-start)*1000)
        time.sleep(2.2)
        play(ROOT/'artifacts/voice-cloud/conversations-03/trial-01/wake.wav',gain=.35)
        poll('agent wake status',lambda x:x['state']=='recording',8)
        start=time.monotonic();r['capture_ack_ms']=cancel_job();r['capture']=listening()
        r['capture_rearm_ms']=round((time.monotonic()-start)*1000)
        assert r['capture']['voice']['turns']==r['idle']['voice']['turns']
        assert d.command('agent context stats',query=True)['events']==r['before']['events']
        d.send('agent voice say 这是取消后的自动恢复测试，播放一小段声音之后会停止，然后设备重新监听。')
        poll('agent audio status',lambda x:x['playing'] and x['rendered']>=24000,60)
        start=time.monotonic();r['playback_ack_ms']=cancel_job()
        r['playback']=listening();r['playback_rearm_ms']=round((time.monotonic()-start)*1000)
        d.command('agent voice off');time.sleep(1)
        r['off']=d.command('agent voice status',query=True)
        r['off_wake']=d.command('agent wake status',query=True)
        assert not r['off']['voice_enabled'] and r['off_wake']['state']=='off'
        r['complete']=True
    finally:
        try:d.command('agent voice off')
        finally:
            d.save();d.close();(a.out/'report.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({k:v for k,v in r.items() if k.endswith('_ms') or k=='complete'}),flush=True)


if __name__=='__main__':main()
