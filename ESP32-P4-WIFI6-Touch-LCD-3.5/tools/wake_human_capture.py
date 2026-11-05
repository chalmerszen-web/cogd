"""Bounded guided human data capture, only after the speaker says they are ready.

Default is a preparation check: no playback, serial connection or recording.
--execute requires the explicit P4 keyword-PCM diagnostic at 921600 baud.
The first four target utterances are TRAIN, the last two are held out; the
two confusable controls are split one each. Labels remain provisional until
content/alignment review, without consulting KWS scores for sample selection.
"""
import argparse,concurrent.futures,hashlib,json,math,sys,time,wave,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tools/voice')]
from device import Device
from record_speaker import Recorder
from wave_play import play,output_devices
from wake_observe import idle,save

def read_wave(path):
    with wave.open(str(path),'rb') as w:
        assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(1,2,16000)
        return w.readframes(w.getnframes())

def main():
    p=argparse.ArgumentParser();p.add_argument('--prompts',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--port',default='COM3');p.add_argument('--execute',action='store_true');a=p.parse_args()
    a.prompts=a.prompts.resolve();a.out=a.out.resolve()
    groups=('nihao_zh','hi_zh','nihao_yue','hi_yue');rate=16000;frames_expected=1125
    required=[a.prompts/(n+'.wav') for n in (*groups,'contrast_nihao','contrast_hi','done')]
    assert all(p.exists() for p in required)
    preparation={'recording_started':False,'groups':list(groups),'frames_per_group':frames_expected,
                 'planned_targets_per_group':6,'planned_controls_per_group':2,
                 'split_rule':'Targets 1-4 TRAIN, 5-6 heldout. Controls 1 TRAIN, 2 heldout. No KWS-based selection.',
                 'prompt_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in required},
                 'output_devices':output_devices()}
    if not a.execute:print(json.dumps(preparation,ensure_ascii=False,indent=2));return
    cache=(ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/build/CMakeCache.txt').read_text(encoding='utf8')
    assert 'AGENT_KEYWORD_PCM:BOOL=ON' in cache,'Build/flash explicit diagnostic first'
    a.out.mkdir(parents=True,exist_ok=False);stop=a.out/'STOP';report=dict(preparation,recording_started=True,complete=False,trials=[])
    d=Device(a.out/'uart',a.port);d.link.baudrate=921600;d.link.timeout=.002
    try:
        until=time.monotonic()+2
        while time.monotonic()<until:d.lines()
        report['system']=d.command('agent status',query=True,timeout=5)
        assert report['system']['device_id']=='esp-hi-e8f60aeb7a3f'
        d.command('agent voice off',timeout=8);d.command('agent cancel',timeout=8)
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            for group in groups:
                if stop.exists():raise InterruptedError('Human capture stopped')
                directory=a.out/group;directory.mkdir();before=idle(d)
                play(a.prompts/(group+'.wav'),gain=.35)
                contrast=read_wave(a.prompts/('contrast_nihao.wav' if group.startswith('nihao') else 'contrast_hi.wav'))
                # Six target cues, spoken contrast instruction, two controls.
                # These cues are recording markers, not phonetic annotations.
                cue_times=[.4+3*i for i in range(6)]
                contrast_at=18.4;control_at=contrast_at+len(contrast)/32000+.4
                cue_times.extend([control_at,control_at+3])
                length=math.ceil((cue_times[-1]+3.1)*rate);schedule=bytearray(length*2)
                start=round(contrast_at*rate)*2;schedule[start:start+len(contrast)]=contrast
                for at in cue_times:
                    for i in range(1440):
                        value=round(5000*math.sin(2*math.pi*880*i/rate)*math.sin(math.pi*i/1440))
                        pos=(round(at*rate)+i)*2;schedule[pos:pos+2]=value.to_bytes(2,'little',signed=True)
                schedule_path=directory/'cues.wav'
                with wave.open(str(schedule_path),'wb') as w:w.setparams((1,2,rate,0,'NONE','not compressed'));w.writeframes(schedule)
                assert 2+length/rate < frames_expected*.032-1
                metadata=[];pcm=bytearray();pending=[];waiting=4;finished=False;future=None;first=None
                with Recorder(directory/'external.wav',seconds=45) as recorder:
                    d.command('agent kws reset-profile',timeout=3);d.command(f'agent keyword pcm arm {frames_expected}',timeout=3)
                    armed=d.command('agent keyword pcm status',query=True,timeout=3);assert armed['active'] and not armed['produced']
                    d.command('agent wake on',timeout=5);deadline=time.monotonic()+frames_expected*.032+5
                    for _ in range(waiting):d.send('agent keyword pcm next')
                    while time.monotonic()<deadline:
                        if stop.exists():raise InterruptedError('Human capture stopped')
                        if not pending:
                            for line in d.lines():
                                if line.startswith('@error'):raise RuntimeError(line)
                                try:row=json.loads(line)
                                except ValueError:continue
                                if isinstance(row,dict) and row.get('pcm')==2:pending.append(row)
                            if not pending:continue
                        row=pending.pop(0);waiting-=1
                        if 'sequence' in row:
                            i=len(metadata);assert row['sequence']==i and row['generation']==armed['generation']
                            assert row['sample_end']==32768+(i+1)*512 and not row['flags']&4
                            raw=bytes.fromhex(row.pop('hex'));assert len(raw)==1024 and zlib.crc32(raw)==row['crc']
                            pcm.extend(raw);metadata.append(row)
                            if first is None:first=time.monotonic()
                        elif row['reason'] not in ('none','complete'):raise RuntimeError(str(row))
                        elif row['reason']=='complete' and not row['reserved']:
                            assert row['produced']==row['consumed']==frames_expected and not row['discarded'] and not row['io_error'];finished=True
                        if first is not None and future is None and time.monotonic()-first>=2:
                            future=pool.submit(play,schedule_path,gain=.35)
                        if len(metadata)<frames_expected or (not finished and not waiting):d.send('agent keyword pcm next');waiting+=1
                        if finished and not waiting:break
                    assert len(metadata)==frames_expected and future is not None
                    playback=future.result(timeout=3);external_started=recorder.started
                after=idle(d);assert after['dma_lost']==before['dma_lost'] and after['wakes']==before['wakes']
                with wave.open(str(directory/'input.wav'),'wb') as w:w.setparams((1,2,rate,0,'NONE','not compressed'));w.writeframes(pcm)
                (directory/'frames.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in metadata),encoding='utf8')
                trial={'group':group,'path':str((directory/'input.wav').relative_to(ROOT)),
                       'external_path':str((directory/'external.wav').relative_to(ROOT)),
                       'pcm_sha256':hashlib.sha256(pcm).hexdigest(),'cue_times':cue_times,'playback':playback,
                       'external_started_host':external_started,'first_frame_host':first,
                       'nominal_splits':['train']*4+['test']*2+['train','test'],
                       'nominal_labels':[1]*6+[0]*2,'needs_content_and_alignment_review':True,
                       'wake_effects_suppressed':True,'before':before,'after':after}
                report['trials'].append(trial);save(a.out/'report.json',report);d.save()
                print(json.dumps({'group':group,'frames':len(metadata),'saved':True}),flush=True)
        report['complete']=True;play(a.prompts/'done.wav',gain=.35)
    except Exception as e:report['error']=repr(e);raise
    finally:
        for cmd in ('agent keyword pcm off','agent cancel','agent voice on','agent wake on'):
            try:d.command(cmd,timeout=8)
            except Exception as e:report.setdefault('cleanup_errors',[]).append(repr(e))
        save(a.out/'report.json',report);d.save();d.close()

if __name__=='__main__':main()
