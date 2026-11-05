"""Finite P4 disconnect, password UI and saved-network regression.
Key commands dispatch the real LVGL editor logic; physical taps remain user QA.
"""
import argparse,json,sys,time,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools/voice'))
from device import Device
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();assert not a.out.exists()
d=Device(a.out,'COM3');d.link.timeout=.03;report={'tap_method':'software dispatch; not physical touch'}
def cmd(c):
 d.lines();return d.command(c,timeout=12)
def q(c):
 try:v=d.command(c,query=True,timeout=12)
 except TimeoutError:v=d.command(c,query=True,timeout=12)
 time.sleep(.03);d.lines();return v
def wait(fn,seconds):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  v=fn()
  if v:return v
  time.sleep(.2)
 raise TimeoutError('Wi-Fi condition deadline')
def snapshot(name):
 subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','dshow','-video_size','1920x1080','-i','video=Insta360 Link',
  '-vf','hflip,vflip','-frames:v','1','-y',str(a.out/(name+'.jpg'))],check=True,timeout=15,creationflags=0x08000000)
def key(n):cmd('agent wifi ui key '+str(n))
try:
 report['before']=q('agent status');assert report['before']['wifi'];cmd('agent voice off');cmd('agent cancel')
 cmd('agent wifi pause');began=time.monotonic();assert not q('agent status')['wifi']
 time.sleep(max(0,28-(time.monotonic()-began)));report['at28']=q('agent wifi ui status');assert not report['at28']['visible']
 wait(lambda:q('agent wifi ui status')['visible'],6);report['appeared_s']=time.monotonic()-began;snapshot('offline-ap-list')
 cmd('agent wifi resume');wait(lambda:q('agent status')['wifi'],30);assert not q('agent wifi ui status')['visible']
 cmd('agent wifi ui show');aps=q('agent wifi ui aps')['aps'];report['aps']=aps
 assert all(aps[i]['rssi']>=aps[i+1]['rssi'] for i in range(len(aps)-1))
 index=next(s['index'] for s in aps if s['ssid']=='gion');cmd('agent wifi ui select '+str(index));snapshot('keyboard-abc')
 key(13);key(11);key(11);snapshot('keyboard-123')
 for _ in range(8):key(0)
 assert q('agent wifi ui status')['editor_length']==8
 key(16);wait(lambda:not q('agent status')['wifi'],5)
 wait(lambda:not q('agent wifi ui status')['joining'],30)
 assert q('agent wifi ui status')['editor_length']==8
 key(15);wait(lambda:q('agent status')['wifi'],30)
 report['after_wrong_password']=q('agent wifi ui status');assert report['after_wrong_password']['saved']==report['at28']['saved']
 cmd('agent wifi ui show');cmd('agent wifi ui select '+str(index));key(13)
 # Enter the user's already-authorized gion password through the nine-key editor.
 key(3);time.sleep(.85)
 for _ in range(3):key(3)
 for _ in range(3):key(5)
 time.sleep(.85)
 for _ in range(2):key(5)
 key(11);key(11)
 for _ in range(4):key(7)
 assert q('agent wifi ui status')['editor_length']==8
 key(16);time.sleep(.5);wait(lambda:q('agent status')['wifi'],30);wait(lambda:not q('agent wifi ui status')['visible'],5)
 report['after_correct']=q('agent wifi ui status');cmd('agent reboot');time.sleep(7)
 wait(lambda:q('agent status')['wifi'],35);report['after_reboot']=q('agent wifi ui status');report['after']=q('agent status');report['passed']=True
finally:
 try:cmd('agent wifi resume');cmd('agent voice on')
 except Exception:pass
 d.save();d.close();(a.out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
