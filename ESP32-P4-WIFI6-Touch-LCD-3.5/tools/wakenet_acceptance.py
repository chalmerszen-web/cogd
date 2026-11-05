"""Finite physical playback regression of the existing dialog lifecycle.

Synthetic speaker replay through the P4 microphone; not human accuracy.
"""
import argparse, concurrent.futures, hashlib, json, sys, time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5'
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tools/voice')]
from device import Device
from wave_play import play
from record_speaker import Recorder
from conversation_test import leveled

p=argparse.ArgumentParser()
p.add_argument('--out',type=Path,required=True)
p.add_argument('--wake',type=Path,required=True)
p.add_argument('--mode',choices=('followup','barge'),required=True)
p.add_argument('--gain',type=float,default=.35)
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
d=Device(a.out,'COM3');d.link.timeout=.02
r={'mode':a.mode,'gain':a.gain,'scope':'Fixed synthetic acoustic playback; no human success-rate claim'}
prompts=P/'evidence/conversation-20261102/tts'
leveled(a.wake,a.out/'wake.wav')

def q(c):return d.command(c,query=True,timeout=10,cancelled_ok=True)
def wait(fn,seconds):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        value=fn()
        if value:return value
        time.sleep(.12)
    raise TimeoutError('Condition not reached')
def event(stage,after):
    d.lines()
    return next((e for e in d.events[after:] if e['stage']==stage),None)
def playback(path,gain):
    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
        f=pool.submit(play,path,gain=gain,preroll_ms=300)
        while not f.done():
            for line in d.lines():
                if 'Guru Meditation' in line or 'assert failed' in line or line.startswith('ESP-ROM:'):
                    raise RuntimeError(line)
        return f.result()
def wake():
    r.setdefault('wakes',[]).append(playback(a.out/'wake.wav',a.gain))
def speak(name):
    path=prompts/(name+'-play.wav')
    r.setdefault('external',[]).append({'name':name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                                      'playback':playback(path,1)})
try:
    d.command('agent voice off',timeout=15)
    wait(lambda:not q('agent status')['busy'],30)
    d.command('agent time set '+str(int(time.time())))
    d.command('agent audio volume 80')
    d.command('agent voice on',timeout=15)
    wait(lambda:q('agent wake status')['state']=='listening',20)
    r['system']=q('agent status');r['before']=q('agent voice dialogue')
    assert r['system']['version']=='0.7.1-p4-wakenet10'
    r['wake_before']=q('agent wake status');r['profile_before']=q('agent kws profile')
    assert r['wake_before']['model']=='wn10_nihaoxiaozhi'
    assert r['wake_before']['threshold']==0 and r['profile_before']['threshold_ppm']==581900
    with Recorder(a.out/'speaker.wav',seconds=230):
        wake();wait(lambda:q('agent wake status')['state']=='recording',10)
        first=len(d.events);speak('newstory' if a.mode=='barge' else 'short')
        if a.mode=='barge':
            wait(lambda:event('llm_first_text',first),70)
            wait(lambda:q('agent audio status')['playing'],20)
            wait(lambda:q('agent audio status')['rendered']>32000,20)
            r['before_barge']=q('agent voice dialogue')
            wake()
            wait(lambda:q('agent voice dialogue')['interrupts']>r['before']['interrupts'],10)
            r['after_barge']=q('agent voice dialogue')
        else:
            wait(lambda:event('done',first),70)
        wait(lambda:q('agent voice dialogue')['followup_ms']>0,20)
        second=len(d.events);speak('recall')
        wait(lambda:event('done',second),70)
        wait(lambda:q('agent voice dialogue')['followup_ms']>0,10)
        r['after_followup']=q('agent voice dialogue')
        assert r['after_followup']['followups']>r['before']['followups']
        r['farewell']=wait(lambda:event('idle_farewell',second),16)
        r['closed']=wait(lambda:event('conversation_closed',second),45)
        wait(lambda:not q('agent status')['busy'],15)
        wait(lambda:q('agent wake status')['state']=='listening',20)
        begins=[e for e in d.events[second:] if e['stage']=='followup_begin']
        r['idle_ms']=r['farewell']['time_ms']-begins[-1]['time_ms']
        assert 9900<=r['idle_ms']<=11000
        r['after']=q('agent voice dialogue')
        assert r['after']['idle_closes']==r['before']['idle_closes']+1
        assert not r['after']['duplex'] and not r['after']['followup_ms']
        r['transport_passed']=True
except Exception as e:
    r['error']=repr(e)
    raise
finally:
    try:
        d.command('agent cancel',timeout=12,cancelled_ok=True)
        d.command('agent voice on',timeout=12)
        r['final_system']=q('agent status');r['final_wake']=q('agent wake status')
        r['final_profile']=q('agent kws profile');r['final_audio']=q('agent audio status')
    except Exception as e:r['cleanup_error']=repr(e)
    r['events']=d.events;d.save();d.close()
    (a.out/'report.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({k:r[k] for k in ('transport_passed','error','idle_ms') if k in r}),flush=True)
