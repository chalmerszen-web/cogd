"""Bounded diagnostic of a saved device clip or explicit TTS, with acoustic evidence."""
import argparse
import json
from pathlib import Path
import sys
import time
from device import Device
from record_speaker import Recorder


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--say')
    p.add_argument('--cancel-stage',choices=('asr_wait','asr_started','asr_audio','asr_partial','tts','playback'))
    p.add_argument('--cancel-delay',type=float,default=0)
    p.add_argument('--mic',action='store_true',help='Exercise the microphone meter concurrently with TTS')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    if not 0<=a.cancel_delay<=10:p.error('Cancel delay must be between zero and ten seconds')
    report={'complete':False,'events':[],'lines':[],'started':time.time()}
    def save():
        (a.out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    d=Device(a.out)
    try:
        d.command('agent voice off');d.command('agent audio volume 80')
        if a.mic:d.command('agent mic on')
        report['before']=d.command('agent status',query=True)
        with Recorder(a.out/'speaker.wav',seconds=220) as recorder:
            report['record_started']=recorder.started
            d.send('agent voice say '+a.say if a.say else 'agent voice run')
            deadline=time.monotonic()+200;next_status=0;terminal=False;cancel_at=None
            while time.monotonic()<deadline and not terminal:
                for line in d.lines():
                    report['lines'].append(line)
                    if line.startswith('@voice '):
                        event=json.loads(line[7:]);event['observed']=time.monotonic()
                        report['events'].append(event)
                        if event['stage']==a.cancel_stage and cancel_at is None:
                            cancel_at=time.monotonic()+a.cancel_delay
                        if event['stage'] in ('done','error'):
                            report['error']=event.get('text') if event['stage']=='error' else None
                    if line.startswith('@done'):terminal=True
                    if line.startswith('@error'):
                        report['error']=line;terminal=True
                    if 'ESP-Hi Agent ready' in line:raise RuntimeError('Unexpected device restart')
                if cancel_at is not None and time.monotonic()>=cancel_at and not report.get('cancel_sent'):
                    report['cancel_sent']=time.monotonic();d.send('agent cancel')
                if not terminal and time.monotonic()>=next_status:
                    d.send('agent status');next_status=time.monotonic()+3
                save()
            if not terminal:raise TimeoutError('Voice transaction did not finish')
            time.sleep(.4)
        d.command('agent voice off')
        for key,command in [('status','agent status'),('audio','agent audio status'),
                            ('voice','agent voice status'),('context','agent context stats')]:
            report[key]=d.command(command,query=True)
        report['complete']=not report.get('error')
        if a.cancel_stage:
            report['complete']=report.get('cancel_sent') is not None and any(
                e['stage']=='error' and e.get('text')=='cancelled' for e in report['events'])
    except BaseException as e:
        report['error']=str(e)
    finally:
        try:d.command('agent voice off')
        except Exception:pass
        if a.mic:
            try:d.command('agent mic off')
            except Exception:pass
        d.save();d.close();report['finished']=time.time();save()
    print(json.dumps({k:report.get(k) for k in ('complete','error','voice','status')},ensure_ascii=False),flush=True)
    if not report['complete']:sys.exit(1)


if __name__=='__main__':main()
