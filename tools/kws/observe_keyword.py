"""Record the exact C3 keyword input; never infer missing USB frames.

Use only with the explicit AGENT_KEYWORD_PCM diagnostic. It suppresses actions,
borrows idle turn scratch, and does not write Flash or send audio to a cloud.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import sys
import time
import wave
import zlib

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/'tools/voice'))
from device import Device
from wave_play import output_devices, play
from acoustic_test import playback_source

def save(path,value):
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

def idle(device):
    device.command('agent wake off',timeout=8)
    deadline=time.monotonic()+8
    while time.monotonic()<deadline:
        wake=device.command('agent wake status',query=True,timeout=2)
        audio=device.command('agent audio status',query=True,timeout=2)
        if wake['state']=='off' and not any(audio.get(key,False) for key in
            ('playing','recording','mic_requested','mic_enabled')):
            return wake
        time.sleep(.02)
    raise TimeoutError('Audio worker did not become idle')

def capture(device,out,frames,source,executor,args,report):
    out.mkdir(exist_ok=False)
    result=dict(frames_expected=frames,source=source,complete=False,started=time.time(),
                frame_metadata=[],events=[],source_playback=None)
    save(out/'report.json',result)
    future=None
    payload=bytearray()
    try:
        before=idle(device)
        device.command('agent kws reset-profile',timeout=3)
        device.command(f'agent keyword pcm arm {frames}',timeout=3)
        armed=device.command('agent keyword pcm status',query=True,timeout=3)
        assert armed['active'] and armed['reserved'] and not armed['produced']
        result['armed']=armed
        device.command('agent wake on',timeout=5)
        end=time.monotonic()+frames*.032+5
        playback_at=None
        with (out/'frames.jsonl').open('x',encoding='utf8') as metadata:
            while time.monotonic()<end:
                row=device.command('agent keyword pcm next',query=True,timeout=2)
                if 'sequence' in row:
                    sequence=len(result['frame_metadata'])
                    assert row['generation']==armed['generation'] and row['sequence']==sequence
                    assert row['sample_end']==32768+(sequence+1)*512
                    raw=bytes.fromhex(row.pop('hex'))
                    assert len(raw)==1024 and zlib.crc32(raw)==row['crc']
                    assert not row['flags']&4, 'Keyword inference reported an error'
                    row['host_received']=time.monotonic()
                    payload.extend(raw)
                    metadata.write(json.dumps(row,ensure_ascii=False)+'\n');metadata.flush()
                    result['frame_metadata'].append(row)
                    if row['flags']&1:result['events'].append(row)
                    if source and playback_at is None:playback_at=time.monotonic()+1
                else:
                    result['last_status']=row
                    if row['reason']!='none' and not row['reserved']:
                        assert row['reason']=='complete' and not row['discarded'] and not row['io_error']
                        assert row['produced']==frames and row['consumed']==frames
                        break
                    if row['reason'] not in ('none','complete'):
                        raise RuntimeError('Observation stopped: '+row['reason'])
                    time.sleep(.001)
                if source and future is None and playback_at is not None and time.monotonic()>=playback_at:
                    path=playback_source(source,args,report)
                    future=executor.submit(play,path,args.device,args.gain)
            else:raise TimeoutError('Observation did not finish within its declared window')
        assert len(result['frame_metadata'])==frames
        if future:result['source_playback']=future.result(timeout=5)
        result['after_wake']=idle(device)
        result['after_status']=device.command('agent status',query=True,timeout=3)
        result['profile']=device.command('agent kws profile',query=True,timeout=3)
        assert result['after_wake']['dma_lost']==before['dma_lost']
        assert result['after_wake']['wakes']==before['wakes'], 'Observation must suppress effects'
        assert result['profile']['blocks']==frames and result['profile']['max_us']<32000
        result['complete']=True
    except Exception as error:
        result['error']=repr(error)
        raise
    finally:
        if future:
            try:result['source_playback']=future.result(timeout=5)
            except Exception as error:result['playback_error']=repr(error)
        result['bytes']=len(payload)
        result['pcm_sha256']=hashlib.sha256(payload).hexdigest()
        (out/'input.pcm').write_bytes(payload)
        with wave.open(str(out/'input.wav'),'wb') as wav:
            wav.setparams((1,2,16000,0,'NONE','not compressed'));wav.writeframes(payload)
        result['ended']=time.time();save(out/'report.json',result)
        print(json.dumps(dict(trial=out.name,complete=result['complete'],frames=len(payload)//1024,
            detections=len(result['events']),error=result.get('error')),ensure_ascii=False),flush=True)
    return result

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--selection',type=Path,required=True)
    parser.add_argument('--port',default='COM5')
    parser.add_argument('--device',type=int)
    parser.add_argument('--gain',type=float,default=.35)
    parser.add_argument('--source-rms',type=float,default=.14)
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=False)
    rows=json.loads(args.selection.read_text(encoding='utf8'))
    assert len(rows)==24 and all(row['split']=='train' for row in rows)
    report=dict(complete=False,started=time.time(),trials=[],gain=args.gain,source_rms=args.source_rms,
        selection_sha256=hashlib.sha256(args.selection.read_bytes()).hexdigest(),
        script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),output_devices=output_devices(),
        limitations='TRAIN replay only; ambient unlabelled; observation suppresses actions; no cloud upload or field/hour acceptance.')
    device=Device(args.out/'usb',args.port)
    # Native USB, not a 115200-baud UART. Short reads let the main task consume
    # 31.25 frames/s without changing the firmware's producer scheduling.
    device.link.timeout=.005
    try:
        report['before']=device.command('agent status',query=True,timeout=5)
        assert report['before']['version']=='0.11.218-kws-input'
        device.command('agent voice off',timeout=8)
        idle(device)
        wake=device.command('agent wake status',query=True,timeout=3)
        assert wake['threshold']==740 and wake['input_gain']==1
        with ThreadPoolExecutor(max_workers=1) as executor:
            report['trials'].append(capture(device,args.out/'ambient',1875,None,executor,args,report))
            save(args.out/'report.json',report)
            for index,row in enumerate(rows):
                path=ROOT/row['path']
                assert hashlib.sha256(path.read_bytes()).hexdigest()==row['source_wav_sha256']
                report['trials'].append(capture(device,args.out/f'trial-{index:02}',188,row,executor,args,report))
                save(args.out/'report.json',report)
        report['complete']=True
    except Exception as error:
        report['error']=repr(error)
        raise
    finally:
        for command in ('agent keyword pcm off','agent wake off','agent cancel'):
            try:device.command(command,timeout=8)
            except Exception as error:report.setdefault('cleanup_errors',[]).append(dict(command=command,error=repr(error)))
        try:
            idle(device)
            report['after']=device.command('agent status',query=True,timeout=3)
            report['context']=device.command('agent context stats',query=True,timeout=3)
        except Exception as error:report['final_state_error']=repr(error)
        device.save();device.close()
        report['ended']=time.time();save(args.out/'report.json',report)

if __name__=='__main__':main()
