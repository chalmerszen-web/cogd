"""One acoustic voice turn while the computer has no open device serial connection."""
import argparse
import json
from pathlib import Path
import time
from device import Device,ROOT
from wave_play import play
from record_speaker import Recorder


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--wait-seconds',type=int,default=85)
    p.add_argument('--prompt-delay',type=float,default=1)
    p.add_argument('--require-enabled',action='store_true',help='Check the saved voice switch after reboot')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False);r={'complete':False}
    assert 35<=a.wait_seconds<=120
    assert .5<=a.prompt_delay<=4.5
    d=Device(a.out/'before')
    try:
        r['boot_voice']=d.command('agent voice status',query=True)
        if a.require_enabled:assert r['boot_voice']['voice_enabled']
        d.command('agent voice on');time.sleep(2)
        r['before']=d.command('agent voice status',query=True)
        assert d.command('agent wake status',query=True)['state']=='listening'
    finally:d.save();d.close()
    source=ROOT/'artifacts/voice-cloud/conversations-03/trial-01'
    r['serial_closed_during_turn']=True
    with Recorder(a.out/'speaker.wav',seconds=a.wait_seconds+15) as recorder:
        r['record_started']=recorder.started
        r['wake_playback']=play(source/'wake.wav',gain=.35)
        time.sleep(a.prompt_delay)
        r['utterance_playback']=play(source/'utterance.wav',gain=.35)
        print('Wake and command played with serial closed; waiting for independent device reply.',flush=True)
        time.sleep(a.wait_seconds)
    d=Device(a.out/'after')
    try:
        r['after']=d.command('agent voice status',query=True)
        r['wake']=d.command('agent wake status',query=True)
        r['status']=d.command('agent status',query=True)
        r['light']=d.command('agent light get',query=True)
        assert r['after']['turns']==r['before']['turns']+1
        assert r['after']['failures']==r['before']['failures']
        assert r['wake']['state']=='listening' and not r['status']['busy']
        assert r['light']=={'r':0,'g':0,'b':255}
        r['complete']=True
    finally:d.save();d.close();(a.out/'report.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({'complete':r['complete'],'elapsed_ms':r['after']['elapsed_ms'],'free_heap':r['status']['free_heap']}),flush=True)


if __name__=='__main__':main()
