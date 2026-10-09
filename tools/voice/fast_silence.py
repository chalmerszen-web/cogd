"""One physical wake followed by no test utterance; bounded endpoint/rearm check.

External audio is retained to identify ambient speech; this does not assert the
room was quiet merely because the script played no command.
"""
import argparse,json,time
from pathlib import Path
from device import Device,ROOT
from wave_play import play
from record_speaker import Recorder

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    d=Device(a.out);r={'complete':False,'test_utterance':None}
    def listening():
        until=time.monotonic()+15
        while time.monotonic()<until:
            wake=d.command('agent wake status',query=True);voice=d.command('agent voice status',query=True)
            if wake['state']=='listening' and voice['fast_ready']:return
        raise TimeoutError('Listening/preconnection did not recover')
    try:
        d.command('agent voice off');d.command('agent voice mode fast');d.command('agent voice on');listening()
        r['before']=d.command('agent voice status',query=True);time.sleep(2.2)
        with Recorder(a.out/'speaker.wav',seconds=25):
            r['wake_playback']=play(ROOT/'artifacts/voice-cloud/conversations-03/trial-01/wake.wav',gain=.35)
            deadline=time.monotonic()+18;terminal=None
            while time.monotonic()<deadline and terminal is None:
                for line in d.lines():
                    if line.startswith('@error') or line=='@done':terminal=line
            r['terminal']=terminal;r['events']=d.events
        listening();r['after']=d.command('agent voice status',query=True)
        r['wake']=d.command('agent wake status',query=True);r['status']=d.command('agent status',query=True)
        r['complete']=terminal=='@error timeout' and not any(e['stage'] in ('playback','tool_done') for e in r['events']) and r['after']['turns']==r['before']['turns']
    finally:
        try:d.command('agent voice off')
        finally:
            d.save();d.close();(a.out/'report.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({'complete':r['complete'],'terminal':r.get('terminal')},ensure_ascii=False))
    if not r['complete']:raise SystemExit(1)
if __name__=='__main__':main()
