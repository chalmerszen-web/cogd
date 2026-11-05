"""Bounded ordinary-firmware false-wake test with screened non-target speech."""
import argparse, concurrent.futures, hashlib, json, re, sys, time, wave
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/voice'),str(ROOT/'tools')]
from device import Device
from wave_play import play

def main():
    p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--app',type=Path,required=True)
    p.add_argument('--firmware',required=True);p.add_argument('--seconds',type=int,default=300)
    p.add_argument('--gain',type=float,default=.35);a=p.parse_args();assert 300<=a.seconds<=600 and 0<a.gain<=.35
    a.out.mkdir(parents=True,exist_ok=False);rows=[r for r in map(json.loads,a.manifest.read_text(encoding='utf8').splitlines()) if not r['label']]
    assert len(rows)>=16;audio=np.zeros(a.seconds*16000,dtype=np.int16);position=32000;segments=[];i=0
    while True:
        row=rows[i%len(rows)];path=ROOT/row['path'];assert hashlib.sha256(path.read_bytes()).hexdigest()==row['wav_sha256']
        with wave.open(str(path),'rb') as w:
            assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(1,2,16000)
            x=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(float)
        x*=min(.14*32768/max(np.sqrt(np.mean(x*x)),1),.8*32768/max(abs(x).max(),1))
        if position+len(x)>len(audio)-32000:break
        audio[position:position+len(x)]=np.rint(x).astype(np.int16);segments.append({'clip_id':row['clip_id'],'seconds':position/16000,'source_sha256':row['wav_sha256']})
        position+=len(x)+32000;i+=1
    wav=a.out/'non-target-background.wav'
    with wave.open(str(wav),'wb') as w:w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(audio.astype('<i2').tobytes())
    chunks=[]
    for offset in range(0,len(audio),60*16000):
        path=a.out/f'playback-{offset//(60*16000):02}.wav'
        with wave.open(str(path),'wb') as w:
            w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(audio[offset:offset+60*16000].astype('<i2').tobytes())
        chunks.append(path)
    def playback():return [play(path,gain=a.gain) for path in chunks]
    r={'complete':False,'seconds':a.seconds,'gain':a.gain,'events':[],'segments':segments,
       'manifest_sha256':hashlib.sha256(a.manifest.read_bytes()).hexdigest(),'app_sha256':hashlib.sha256(a.app.read_bytes()).hexdigest(),
       'scope':'Fixed speaker; repeated independently screened non-target speech and room background, no general false-alarm-rate guarantee'}
    d=Device(a.out,'COM3')
    def save():(a.out/'report.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8');d.save()
    try:
        system=d.command('agent status',query=True,timeout=8);assert system['device_id']=='esp-hi-e8f60aeb7a3f' and system['version']==a.firmware
        d.command('agent voice off',timeout=12);d.command('agent cancel',timeout=8);d.command('agent wake on',timeout=8)
        time.sleep(3.3);before=d.command('agent wake status',query=True,timeout=4);assert before['state']=='listening';r['before']=before
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            start=time.monotonic();future=pool.submit(playback);last=before['wakes']
            while not future.done():
                state=d.command('agent wake status',query=True,timeout=4)
                assert state['wakes']>=last and state['dma_lost']==before['dma_lost']
                if state['wakes']>last:
                    r['events'].append({'elapsed':time.monotonic()-start,'delta':state['wakes']-last});last=state['wakes']
                    d.command('agent cancel',timeout=8);d.command('agent wake on',timeout=8)
                save();time.sleep(.2)
            r['playback']=future.result();time.sleep(1)
        r['after']=d.command('agent wake status',query=True,timeout=4)
        r['false_wakes']=r['after']['wakes']-before['wakes'];r['complete']=r['false_wakes']==0
        assert hashlib.sha256(a.app.read_bytes()).hexdigest()==r['app_sha256']
    except Exception as error:r['error']=str(error);raise
    finally:
        try:d.command('agent cancel',timeout=8);d.command('agent voice on',timeout=8);d.command('agent wake on',timeout=8)
        finally:save();d.close()
    print(json.dumps({'complete':r['complete'],'false_wakes':r['false_wakes'],'seconds':a.seconds}));assert r['complete']

if __name__=='__main__':main()
