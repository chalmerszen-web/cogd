"""P4-only 921600-baud diagnostic of exact live KWS PCM and integer scores.

This requires build.ps1 -KeywordPcm, suppresses wake actions, and is not a
substitute for the ordinary-firmware acoustic and dialogue acceptance tests.
"""
import argparse, concurrent.futures, hashlib, json, shutil, sys, time, wave, zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/voice'),str(ROOT/'tools')]
from device import Device
from wave_play import play
from conversation_test import leveled

def save(path,data):
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')

def idle(d):
    d.command('agent wake off',timeout=8)
    deadline=time.monotonic()+8
    while time.monotonic()<deadline:
        w=d.command('agent wake status',query=True,timeout=3)
        a=d.command('agent audio status',query=True,timeout=3)
        if w['state']=='off' and not any(a.get(k,False) for k in ('playing','recording','mic_requested','mic_enabled')):return w
    raise TimeoutError('Audio worker did not become idle')

def main():
    p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--port',default='COM3');p.add_argument('--frames',type=int,default=250);p.add_argument('--warmup',type=float,default=3.3)
    p.add_argument('--gain',type=float,default=.35);p.add_argument('--repeat',type=int,default=1)
    p.add_argument('--development',action='store_true',help='Explicit diagnostic-only validation capture; never admissible as TRAIN')
    p.add_argument('--startup-settle-seconds',type=float,default=12)
    p.add_argument('--app',type=Path,default=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/build/cogd_p4.bin')
    a=p.parse_args()
    a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    rows=[json.loads(s) for s in a.manifest.read_text(encoding='utf8').splitlines() if s.strip()]
    assert rows and all(r['split']==('validation' if a.development else 'train') for r in rows)
    report=dict(complete=False,scope='Diagnostic DEVELOPMENT; actual PCM; effects suppressed; NOT TRAIN' if a.development else 'Diagnostic TRAIN; actual PCM; effects suppressed',trials=[],
                gain=a.gain,warmup=a.warmup,manifest_sha256=hashlib.sha256(a.manifest.read_bytes()).hexdigest(),
                app_sha256=hashlib.sha256(a.app.read_bytes()).hexdigest())
    report['observer_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    report['request_cadence_ms']=0
    d=Device(a.out/'uart',a.port);d.link.baudrate=921600;d.link.timeout=.002
    try:
        end=time.monotonic()+a.startup_settle_seconds
        while time.monotonic()<end:d.lines()
        report['system']=d.command('agent status',query=True,timeout=5)
        assert report['system']['device_id']=='esp-hi-e8f60aeb7a3f'
        d.command('agent voice off',timeout=8);d.command('agent cancel',timeout=8)
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            for rep in range(a.repeat):
                for source in rows:
                    directory=a.out/f'{rep:02}-{source["clip_id"]}';directory.mkdir()
                    path=ROOT/source['path'];assert hashlib.sha256(path.read_bytes()).hexdigest()==source['wav_sha256']
                    if source.get('prepared_level'):
                        shutil.copy2(path,directory/'playback.wav');level={'prepared_level':True,'normalization_applied':False}
                    else:level=leveled(path,directory/'playback.wav')
                    before=idle(d);assert before['threshold']==740 and before['input_gain']==1
                    d.command('agent kws reset-profile',timeout=3);d.command(f'agent keyword pcm arm {a.frames}',timeout=3)
                    armed=d.command('agent keyword pcm status',query=True,timeout=3)
                    assert armed['active'] and armed['reserved'] and not armed['produced']
                    d.command('agent wake on',timeout=5)
                    pcm=bytearray();frames=[];future=None;first=None;deadline=time.monotonic()+a.frames*.032+9
                    # UART command reads have a bounded read timeout. Keep four
                    # requests in flight so that timeout is not paid per frame.
                    waiting=4;pending=[];finished=False
                    for _ in range(waiting):d.send('agent keyword pcm next')
                    while time.monotonic()<deadline:
                        if not pending:
                            for line in d.lines():
                                if line.startswith('@error'):raise RuntimeError(line)
                                try:value=json.loads(line)
                                except ValueError:continue
                                if isinstance(value,dict) and value.get('pcm')==2:pending.append(value)
                            if not pending:continue
                        row=pending.pop(0);waiting-=1
                        if 'sequence' in row:
                            i=len(frames);assert row['generation']==armed['generation'] and row['sequence']==i
                            assert row['sample_end']==32768+(i+1)*512
                            raw=bytes.fromhex(row.pop('hex'));assert len(raw)==1024 and zlib.crc32(raw)==row['crc']
                            assert not row['flags']&4
                            pcm.extend(raw);frames.append(row)
                            if first is None:first=time.monotonic()
                        elif row['reason'] not in ('none','complete'):raise RuntimeError(str(row))
                        elif row['reason']=='complete' and not row['reserved']:
                            assert row['produced']==row['consumed']==a.frames and not row['discarded'] and not row['io_error'];finished=True
                        if first is not None and future is None and time.monotonic()-first>=a.warmup:
                            future=pool.submit(play,directory/'playback.wav',gain=float(source.get('playback_gain',a.gain)))
                        if len(frames)<a.frames or (not finished and not waiting):
                            d.send('agent keyword pcm next');waiting+=1
                        if finished and not waiting:break
                    assert len(frames)==a.frames
                    playback=future.result(timeout=5);after=idle(d)
                    profile=d.command('agent kws profile',query=True,timeout=3)
                    checks={'dma_unchanged':after['dma_lost']==before['dma_lost'],
                            'actions_suppressed':after['wakes']==before['wakes'],
                            'exact_block_count':profile['blocks']==a.frames,'real_time':profile['max_us']<32000}
                    with wave.open(str(directory/'input.wav'),'wb') as w:
                        w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(pcm)
                    (directory/'frames.jsonl').write_text(''.join(json.dumps(f)+'\n' for f in frames),encoding='utf8')
                    trial=dict(source=source,path=(directory/'input.wav').relative_to(ROOT).as_posix(),levels=level,playback=playback,
                               pcm_sha256=hashlib.sha256(pcm).hexdigest(),frames=len(frames),detections=[f for f in frames if f['flags']&1],
                               profile=profile,before=before,after=after)
                    trial['checks']=checks;trial['valid']=all(checks.values())
                    report['trials'].append(trial);save(a.out/'report.json',report);d.save()
                    print(json.dumps(dict(id=source['clip_id'],repeat=rep,detections=len(trial['detections']),score_max=max(f['score_q8'] for f in frames))),flush=True)
                    assert trial['valid'],{k:v for k,v in checks.items() if not v}
        report['complete']=True
    except Exception as e:report['error']=repr(e);raise
    finally:
        for cmd in ('agent keyword pcm off','agent cancel','agent voice on','agent wake on'):
            try:d.command(cmd,timeout=8)
            except Exception as e:report.setdefault('cleanup_errors',[]).append(repr(e))
        save(a.out/'report.json',report);d.save();d.close()

if __name__=='__main__':main()
