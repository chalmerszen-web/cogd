"""Record finite source-labelled P4 microphone examples using lossless clips.

This diagnostic stops KWS; it is training/diagnosis, not wake acceptance.
The speaker, codec gain, sample format and raw PCM path match wake input.
"""
import argparse, concurrent.futures, hashlib, json, sys, time, wave
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/voice'),str(ROOT/'tools')]
from device import Device
from wave_play import play
from conversation_test import leveled
from export_audio_clip import export_clip

def main():
    p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--gain',type=float,default=.35);p.add_argument('--port',default='COM3');a=p.parse_args()
    a.out=a.out.resolve()
    rows=[json.loads(s) for s in a.manifest.read_text(encoding='utf8').splitlines() if s.strip()]
    assert 0<len(rows)<=64 and all(r['split'] in ('train','validation') for r in rows)
    a.out.mkdir(parents=True,exist_ok=False);d=Device(a.out/'usb',a.port);d.link.timeout=.01
    report={'complete':False,'started':time.time(),'manifest_sha256':hashlib.sha256(a.manifest.read_bytes()).hexdigest(),'trials':[]}
    def status(): return d.command('agent audio status',query=True,timeout=5)
    def save():
        (a.out/'capture-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');d.save()
    try:
        report['system']=d.command('agent status',query=True,timeout=6)
        assert report['system']['device_id']=='esp-hi-e8f60aeb7a3f'
        d.command('agent voice off',timeout=8);d.command('agent cancel',timeout=8);d.command('agent wake off',timeout=8)
        time.sleep(.6)
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            for r in rows:
                source=ROOT/r['path']
                assert hashlib.sha256(source.read_bytes()).hexdigest()==r['wav_sha256']
                target=a.out/r['clip_id'];target.mkdir()
                level=leveled(source,target/'playback.wav')
                with wave.open(str(target/'playback.wav'),'rb') as w: seconds=w.getnframes()/w.getframerate()
                assert seconds<=3.1
                capture_ms=min(4000,max(2000,int((seconds+.8)*1000+15)//16*16))
                d.command(f'agent audio capture {capture_ms}',timeout=5)
                until=time.monotonic()+10
                while status()['capture_stage']!=2:
                    if time.monotonic()>until: raise TimeoutError('ADC capture did not start')
                    time.sleep(.03)
                time.sleep(.35)
                playback=pool.submit(play,target/'playback.wav',gain=a.gain)
                while status()['recording']:
                    if time.monotonic()>until+8: raise TimeoutError('ADC capture did not complete')
                    time.sleep(.15)
                after=status();assert after['capture_error']=='ok',after
                captured=export_clip(d,target,create=False,deadline=time.monotonic()+75)
                trial={'source':r,'playback':playback.result(timeout=5),'levels':level,'capture':captured,
                       'path':(target/'device-clip.wav').relative_to(ROOT).as_posix()}
                report['trials'].append(trial);save()
                print(json.dumps({'n':len(report['trials']),'total':len(rows),'id':r['clip_id'],'split':r['split']},ensure_ascii=False),flush=True)
        report['complete']=True
    except Exception as e:report['error']=repr(e);raise
    finally:
        try:d.command('agent cancel',timeout=8);d.command('agent voice on',timeout=8);d.command('agent wake on',timeout=8)
        finally:report['ended']=time.time();save();d.close()

if __name__=='__main__': main()
