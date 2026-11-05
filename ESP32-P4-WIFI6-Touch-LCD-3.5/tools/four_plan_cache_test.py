"""Ten completed cache replays plus offline Wi-Fi overlay/audio ownership."""
import argparse,json,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools/voice'))
from device import Device
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
assert not a.out.exists();d=Device(a.out,'COM3');d.link.timeout=.02;r={'tap_method':'actual handler, software dispatch','replays':[]}
def q(c):return d.command(c,query=True,timeout=10)
def media():return q('agent media status')
def wait(fn,seconds):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  v=fn()
  if v:return v
  time.sleep(.08)
 raise TimeoutError('Cache test deadline')
try:
 d.command('agent voice off');assert media()['state']=='ended'
 for i in range(10):
  old=media()['replays'];at=time.monotonic();d.command('agent media tap')
  wait(lambda:media()['replays']>old,3);wait(lambda:media()['position_ms']>0,3)
  first=time.monotonic()-at;wait(lambda:media()['state']=='ended',18)
  r['replays'].append({'n':i+1,'observed_start_s':first,'after':media()})
  print('completed replay',i+1,flush=True)
 d.command('agent wifi pause');wait(lambda:not q('agent status')['wifi'],3)
 time.sleep(28);assert not q('agent wifi ui status')['visible']
 old=media()['replays'];d.command('agent media tap');wait(lambda:media()['replays']>old,3)
 wait(lambda:q('agent wifi ui status')['visible'],5);paused=wait(lambda:(m:=media())['state']=='paused' and m,3)
 time.sleep(.5);assert media()['position_ms']==paused['position_ms']
 d.command('agent media tap');time.sleep(.25);assert media()['state']=='paused'
 r['overlay_pause']=paused;r['overlay_blocks_video_tap']=True
 d.command('agent wifi resume');wait(lambda:q('agent status')['wifi'],30)
 wait(lambda:not q('agent wifi ui status')['visible'],3)
 d.command('agent media tap');wait(lambda:media()['state']=='ended',10)
 r['after']=q('agent status');r['passed']=True
except Exception as e:r['error']=repr(e);raise
finally:
 try:d.command('agent wifi resume');d.command('agent voice on')
 except Exception:pass
 d.save();d.close();(a.out/'report.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
