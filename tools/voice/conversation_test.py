"""Computer speaker -> real device wake/VAD/ASR/DeepSeek/TTS, with local acoustic evidence."""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import sys
import time
import wave
from device import Device,ROOT
from wave_play import play
from record_speaker import Recorder


def leveled(source,target):
    with wave.open(str(source),'rb') as w:
        assert w.getsampwidth()==2 and w.getnchannels()==1
        rate=w.getframerate();pcm=array.array('h',w.readframes(w.getnframes()))
    rms=math.sqrt(sum(x*x for x in pcm)/len(pcm))/32768;peak=max(abs(x) for x in pcm)/32768
    gain=min(.14/max(rms,1e-8),.8/max(peak,1e-8))
    data=array.array('h',(round(x*gain) for x in pcm))
    with wave.open(str(target),'wb') as w:
        w.setparams((1,2,rate,0,'NONE','not compressed'));w.writeframes(data.tobytes())
    block=max(1,round(rate*.01))
    energies=[math.sqrt(sum(x*x for x in data[i:i+block])/len(data[i:i+block]))
              for i in range(0,len(data),block)]
    threshold=max(64,max(energies)*.02)
    active=[i for i,e in enumerate(energies) if e>=threshold]
    return dict(source=str(source),source_sha256=hashlib.sha256(Path(source).read_bytes()).hexdigest(),gain=gain,rms=rms,
        active_end_s=min(len(data),(active[-1]+1)*block)/rate if active else None,
        active_rule='10ms source RMS >= max(64, 2% peak block RMS); not an acoustic endpoint',
        active_threshold_rms=threshold)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--rounds',type=int,default=3);p.add_argument('--offset',type=int,default=0)
    p.add_argument('--prompt-manifest',type=Path,default=ROOT/'artifacts/voice-cloud/prompts-01/manifest.json')
    p.add_argument('--firmware',help='Require this exact version when comparing firmware builds')
    p.add_argument('--mode',choices=('fast','classic'),default='classic')
    p.add_argument('--capture-output',choices=('hold','off'),help='Volatile PDM/PA capture diagnostic; restored to hold after test')
    p.add_argument('--rearm-gap',type=float,help='Seconds after each completed turn before the next wake; skips ready gate on later turns')
    p.add_argument('--keep-going',action='store_true',help='Retain content failures and complete the bounded group')
    p.add_argument('--tx-power',type=int,help='Temporary Wi-Fi power limit, SDK quarter-dBm units (8..84)')
    a=p.parse_args();assert 1<=a.rounds<=30
    if a.rearm_gap is not None and not 0<=a.rearm_gap<=10:p.error('rearm-gap must be 0..10')
    if a.tx_power is not None and not 8<=a.tx_power<=84:p.error('tx-power must be 8..84')
    a.out.mkdir(parents=True,exist_ok=False)
    prompts=json.loads(a.prompt_manifest.read_text(encoding='utf8'))
    assert prompts['complete']
    rows=[json.loads(x) for x in (ROOT/'artifacts/kws-phase6/replay-selection.jsonl').read_text(encoding='utf8').splitlines()]
    wakes={lang:next(r for r in rows if r['label']==1 and r['language']==lang) for lang in ('zh','yue')}
    report={'complete':False,'started':time.time(),'synthetic':True,'mode':a.mode,'trials':[]}
    def save(): (a.out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    d=Device(a.out);save()
    try:
        d.command('agent voice off');d.command('agent voice mode '+a.mode)
        if a.capture_output:
            d.command('agent voice capture-output '+a.capture_output)
            report['capture_output']=d.command('agent wake status',query=True)
            if report['capture_output'].get('capture_output_hold')!=(a.capture_output=='hold'):
                raise RuntimeError('Capture output diagnostic did not apply')
        d.command('agent audio volume 80')
        if a.tx_power is not None:
            report['radio_before']=d.command('agent status',query=True)
            if 'wifi_tx_quarter_dbm' not in report['radio_before']:raise RuntimeError('Firmware lacks radio diagnostic')
            d.command('agent wifi power '+str(a.tx_power))
        report['before']=d.command('agent status',query=True);save()
        if a.firmware and report['before']['version']!=a.firmware:
            raise RuntimeError('Unexpected firmware: '+report['before']['version'])
        d.command('agent voice on')
        for index in range(a.rounds):
            number=index+a.offset;prompt=prompts['prompts'][number%len(prompts['prompts'])]
            lang=('zh','yue')[number%2];source=wakes[lang]
            folder=a.out/f'trial-{index+1:02}';folder.mkdir()
            trial={'id':prompt['id'],'wake_language':lang,'events':[],'lines':[],'started':time.time(),'source':prompt}
            event_start=len(d.events)
            report['trials'].append(trial)
            trial['wake_source']=leveled(ROOT/source['path'],folder/'wake.wav')
            trial['utterance_source']=leveled(ROOT/prompt['path'],folder/'utterance.wav');save()
            immediate=index>0 and a.rearm_gap is not None
            if immediate:
                trial['rearm_gap']=a.rearm_gap
                trial['before_next_wake']=d.command('agent wake status',query=True)
                trial['before_next_voice']=d.command('agent voice status',query=True)
                time.sleep(a.rearm_gap)
            else:
                deadline=time.monotonic()+15
                while time.monotonic()<deadline:
                    state=d.command('agent wake status',query=True)
                    voice=d.command('agent voice status',query=True) if a.mode=='fast' else {}
                    if state['state']=='listening' and (voice.get('preconnect')=='capture' or voice.get('fast_ready',True)):break
                    time.sleep(.15)
                else:raise TimeoutError('Wake model did not start')
                time.sleep(2.2)
            with Recorder(folder/'speaker.wav',seconds=220) as recorder:
                trial['record_started']=recorder.started
                trial['wake_playback']=play(folder/'wake.wav',gain=.35)
                deadline=time.monotonic()+8
                while time.monotonic()<deadline:
                    state=d.command('agent wake status',query=True)
                    if state['state']=='recording':break
                    time.sleep(.1)
                else:raise TimeoutError('Wake did not reach recording')
                trial['recording_state']=state
                trial['utterance_playback']=play(folder/'utterance.wav',gain=.35);save()
                active_end=trial['utterance_source']['active_end_s']
                trial['source_active_end_host_estimate']=trial['utterance_playback']['source_started']+active_end if active_end is not None else None
                deadline=time.monotonic()+200;next_status=0;terminal=False
                while time.monotonic()<deadline and not terminal:
                    for line in d.lines():
                        trial['lines'].append(line)
                        if line.startswith('@voice '):
                            e=json.loads(line[7:]);e['observed']=time.monotonic();trial['events'].append(e)
                            if e['stage'] in ('done','error'):
                                trial['error']=e.get('text') if e['stage']=='error' else None
                        if line.startswith('@done'):terminal=True
                        if line.startswith('@error'):terminal=True;trial['error']=line
                        if 'ESP-Hi Agent ready' in line:raise RuntimeError('Unexpected device restart')
                    if not terminal and time.monotonic()>=next_status:
                        d.send('agent status');next_status=time.monotonic()+3
                    save()
                if not terminal:raise TimeoutError('Voice transaction did not finish')
                time.sleep(.4)
            trial['status']=d.command('agent status',query=True)
            trial['audio']=d.command('agent audio status',query=True)
            trial['wake']=d.command('agent wake status',query=True)
            trial['voice']=d.command('agent voice status',query=True)
            rearm_deadline=time.monotonic()+15
            while a.rearm_gap is None and a.mode=='fast' and time.monotonic()<rearm_deadline and (
                    trial['wake']['state']!='listening' or (trial['voice'].get('preconnect')!='capture' and not trial['voice'].get('fast_ready',True))):
                time.sleep(.2)
                trial['wake']=d.command('agent wake status',query=True)
                trial['voice']=d.command('agent voice status',query=True)
            trial['light']=d.command('agent light get',query=True)
            # Capture-time stream events can arrive inside status queries too.
            trial['events']=list(d.events[event_start:])
            stages=[e['stage'] for e in trial['events']]
            required=('vad_end','asr_text','playback','done') if a.mode=='fast' else ('vad_end','asr_text','llm','tts','playback','done')
            trial['complete']=not trial.get('error') and all(s in stages for s in required)
            transcript=next((e.get('text','') for e in trial['events'] if e['stage']=='asr_text'),'')
            expected=list(prompt.get('required',[]))
            if '请简单回答' in prompt['text']:expected+=['简单','回答']
            if prompt['id']=='remember':expected+=['记住','小星星']
            trial['input_checks']={word:word in transcript for word in expected}
            if not all(trial['input_checks'].values()):
                trial['error']='ASR omitted required instruction content'
                trial['complete']=False
            if a.mode=='fast' and prompt['id'] in ('remember','recall_name'):
                trial['history_route_checked']='delegate_deepseek' in stages
                if not trial['history_route_checked']:
                    trial['error']='Historical request did not reach the persistent-context route'
                    trial['complete']=False
            colors={'blue':{'r':0,'g':0,'b':255},'green':{'r':0,'g':255,'b':0}}
            if prompt['id'] in colors:
                word={'blue':'蓝','green':'绿'}[prompt['id']]
                trial['semantic_checks']={'color_in_transcript':word in transcript,
                    'actual_color':trial['light']==colors[prompt['id']]}
                if not all(trial['semantic_checks'].values()):
                    trial['error']='Incomplete transcription or wrong physical tool result'
                    trial['complete']=False
            trial['acoustic_latency']={'source_recording':str(folder/'speaker.wav'),
                'source_active_end_s':active_end,'useful_speech_latency_s':None,'within_one_second':None,
                'status':'requires_independent_source_and_reply_alignment',
                'note':'Match utterance.wav in speaker.wav, then identify reply speech excluding cues/background. First PCM/playback telemetry is not audible or useful speech.'}
            trial['rearmed']=trial['wake']['state']=='listening' and trial['voice']['voice_enabled']
            # In immediate-rewake tests readiness is the measured outcome, not
            # a hidden wait condition. The next physical wake tests it directly.
            if a.rearm_gap is None:trial['complete']=trial['complete'] and trial['rearmed']
            trial['finished']=time.time();save()
            print(json.dumps({'trial':index+1,'id':prompt['id'],'complete':trial['complete'],
                'asr':next((e.get('text') for e in trial['events'] if e['stage']=='asr_text'),None),
                'free_heap':trial['status']['free_heap'],'min_heap':trial['status']['min_heap'],'error':trial.get('error')},ensure_ascii=False),flush=True)
            if not trial['complete'] and not a.keep_going:raise RuntimeError('Voice pipeline failed; stop batch for diagnosis')
        report['complete']=all(t.get('complete',False) for t in report['trials'])
    except BaseException as e:
        report['error']=str(e)
        if report['trials']:report['trials'][-1].setdefault('error',str(e))
        print(type(e).__name__+': '+str(e),flush=True)
    finally:
        try:
            d.command('agent voice off')
            if a.capture_output:d.command('agent voice capture-output hold')
            if 'radio_before' in report:
                d.command('agent wifi power '+str(report['radio_before']['wifi_tx_quarter_dbm']))
                report['radio_restored']=d.command('agent status',query=True)
        except Exception:pass
        d.save();d.close();report['finished']=time.time();save()
    if not report['complete']:sys.exit(1)


if __name__=='__main__':main()
