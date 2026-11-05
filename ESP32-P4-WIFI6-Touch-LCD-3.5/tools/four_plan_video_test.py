"""One paid WAN request, then bounded original-file/cache/touch-state tests.
Serial tap dispatch tests the same handler; it does not prove a physical tap.
"""
import argparse,json,sys,time,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/voice'))
from device import Device
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--task');p.add_argument('--url');p.add_argument('--cached',action='store_true');a=p.parse_args()
assert not a.out.exists();a.out.mkdir(parents=True)
d=Device(a.out,'COM3');d.link.timeout=.03
result={'tap_method':'software dispatch of actual touch handler','no_pc_transcode':True,'samples':[]}
camera_log=(a.out/'camera.log').open('wb')
cam=subprocess.Popen(['ffmpeg','-hide_banner','-f','dshow','-rtbufsize','256M','-video_size','1920x1080','-framerate','30',
 '-i','video=Insta360 Link:audio=麦克风 (Misiom-Shooter)','-vf','hflip,vflip','-c:v','libx264','-preset','veryfast','-crf','28',
 '-c:a','pcm_s16le','-ac','1','-ar','16000','-y',str(a.out/'camera.mkv')],stdin=subprocess.PIPE,stdout=camera_log,stderr=camera_log,creationflags=0x08000000)
def q(c):
 # Media owns the engine during actual audio output. Only read-only queries may
 # be retried; a model POST or a toggle must never be submitted twice.
 readonly=c in ('agent status','agent media status','agent tool qianwen_skill {"action":"status"}')
 until=time.monotonic()+15
 while True:
  try:return d.command(c,query=True,timeout=10)
  except (TimeoutError,RuntimeError) as e:
   if not readonly or time.monotonic()>=until or (isinstance(e,RuntimeError) and '@error busy' not in str(e)):raise
   time.sleep(.2)
def media():return q('agent media status')
def wait(fn,seconds):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  v=fn()
  if v:return v
  time.sleep(.08)
 raise TimeoutError('Condition deadline')
try:
 d.command('agent voice off')
 if not a.cached:d.command('agent cancel');d.command('agent time set '+str(int(time.time())))
 result['before']=q('agent status');assert result['before']['wifi']
 req={'task_id':a.task} if a.task else {'url':a.url} if a.url else {'prompt':'一只小猫在草地上踢足球，踢球时有清晰的碰撞声。','duration':5}
 req.update(fps=15,brightness=15);result['request']=req;result['started']=time.time()
 if not a.cached:result['submit']=q('agent tool device_display_video '+json.dumps(req,ensure_ascii=False))
 else:result['request']={'reuse_existing_cache':True}
 end=time.monotonic()+1000
 while time.monotonic()<end:
  state=q('agent tool qianwen_skill {"action":"status"}');mem=q('agent status')
  result['samples'].append({'at':time.time(),'job':state,'memory':mem});print(json.dumps(state,ensure_ascii=False),flush=True)
  (a.out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
  if state.get('state') in ('played','failed','prepare_failed','acceptance_unknown','error'):break
  time.sleep(2)
 assert state['state']=='played',state
 result['first']=state;assert media()['state']=='ended'
 previous=media()['replays'];start=time.monotonic();d.command('agent media tap');wait(lambda:media()['replays']>previous,3);wait(lambda:media()['position_ms']>0,3)
 result['replay_started_s']=time.monotonic()-start
 result['tap_cycles']=[]
 for i in range(10):
  if media()['state']=='ended':
   old=media()['replays'];d.command('agent media tap');wait(lambda:media()['replays']>old,3);wait(lambda:media()['position_ms']>0,3)
  start=time.monotonic();d.command('agent media tap');paused=wait(lambda:(s:=media())['state']=='paused' and s,2)
  delay=30 if i==0 else .3;time.sleep(delay);still=media()
  assert still['state']=='paused' and still['position_ms']==paused['position_ms'],(paused,still)
  result['tap_cycles'].append({'pause_ack_s':time.monotonic()-start-delay,'position':paused['position_ms'],'duration_s':delay})
  d.command('agent media tap');wait(lambda:media()['state'] in ('playing','ended'),2);time.sleep(.1)
 wait(lambda:media()['state']=='ended',10)
 d.command('agent wifi pause');time.sleep(.4);assert not q('agent status')['wifi']
 old=media()['replays'];d.command('agent media tap');wait(lambda:media()['replays']>old,3);wait(lambda:media()['state']=='ended',10)
 result['offline_replay']=True;d.command('agent wifi resume');wait(lambda:q('agent status')['wifi'],30)
 result['after']=q('agent status');result['passed']=True
except Exception as e:result['error']=repr(e);raise
finally:
 try:d.command('agent wifi resume');d.command('agent voice on')
 except Exception:pass
 d.save();d.close()
 if cam.poll() is None:cam.communicate(b'q',timeout=20)
 camera_log.close();result['camera_exit']=cam.returncode
 (a.out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
