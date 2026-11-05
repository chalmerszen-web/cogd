"""Finite source-labelled actual microphone/DAC/AEC captures, not an accuracy test."""
import argparse,json,time,sys,wave,hashlib,concurrent.futures
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tools/voice')]
from device import Device
from wave_play import play
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out=a.out.resolve()
assert not a.out.exists();a.out.mkdir(parents=True)
rows=[json.loads(s) for s in a.manifest.read_text(encoding='utf8').splitlines()]
assert 0<len(rows)<=24
d=Device(a.out/'usb','COM3');d.link.timeout=.01;report={'scope':'actual fixed-speaker mic/AEC capture; not human/angle/SPL acceptance','trials':[]}
try:
 d.command('agent voice off');d.command('agent cancel');d.command('agent audio volume 80')
 with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
  for i,row in enumerate(rows):
   path=ROOT/row['path'];assert hashlib.sha256(path.read_bytes()).hexdigest()==row['wav_sha256']
   target=a.out/f'{i:02d}';target.mkdir()
   if row['overlap']:
    d.send('agent voice say 小猫今天在草地上练习踢足球，小狗在旁边看着，它们准备下午一起出去散步。')
    end=time.monotonic()+25
    while not d.command('agent audio status',query=True)['playing']:
     if time.monotonic()>end:raise TimeoutError('TTS did not start')
    time.sleep(1)
   d.command('agent audio probe start 4000');time.sleep(.35)
   playback=pool.submit(play,path,gain=row['gain'])
   time.sleep(4);status=d.command('agent audio probe status',query=True);assert status['samples']==status['target']==64000
   raw=bytearray()
   for offset in range(0,status['samples']*6,8000):
    data=d.command('agent audio probe read '+str(offset),query=True,timeout=4);assert data['offset']==offset
    raw.extend(bytes.fromhex(data['hex']))
   d.command('agent audio probe free')
   x=np.frombuffer(raw,dtype='<i2').reshape(-1,3)
   for ch,name in enumerate(('mic','reference','aec')):
    with wave.open(str(target/(name+'.wav')),'wb') as w:w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(x[:,ch].tobytes())
   report['trials'].append({'source':row,'playback':playback.result(),'path':(target/'aec.wav').relative_to(ROOT).as_posix(),
       'channel_rms':np.sqrt(np.mean(x.astype(float)**2,axis=0)).tolist(),'source_split':row['split']})
   d.save();(a.out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
   print(json.dumps({'n':i+1,'total':len(rows),'group':row['wake_group'],'overlap':row['overlap'],'rms':report['trials'][-1]['channel_rms']}),flush=True)
 report['complete']=True
finally:
 try:d.command('agent audio probe free');d.command('agent cancel');d.command('agent voice on')
 finally:d.save();d.close();(a.out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
