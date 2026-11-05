"""Bounded live receipt checks. One request per operation, no paid retries."""
import argparse,json,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/voice'))
from device import Device
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
assert not a.out.exists();d=Device(a.out,'COM3');d.link.timeout=.03
r={'scope':'current live LLM, search and image adapters; no all-model paid sweep'}
def q(c,timeout=12):return d.command(c,query=True,timeout=timeout)
def wait(fn,seconds):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  v=fn()
  if v:return v
  time.sleep(.3)
 raise TimeoutError('Bounded model test deadline')
try:
 d.command('agent cancel');d.command('agent voice off');d.command('agent time set '+str(int(time.time())))
 r['before']=q('agent status');assert r['before']['wifi']
 d.send('agent chat 用一个短句说明离线时可以通过触屏选择WiFi，不要调用任何工具。')
 wait(lambda:q('agent status')['busy'],5);wait(lambda:not q('agent status')['busy'],90)
 r['llm_finished']=True
 r['search']=q('agent tool device_web_search '+json.dumps({'query':'ESP32-P4 官方芯片支持的 MIPI CSI 摄像头接口是什么？一句话说明并附官方来源。'},ensure_ascii=False),90)
 r['image_submit']=q('agent tool device_display_image '+json.dumps({'prompt':'白色背景，中央一个蓝色圆形，极简平面图案，不要文字。','brightness':15},ensure_ascii=False))
 r['image_samples']=[]
 end=time.monotonic()+240
 while time.monotonic()<end:
  v=q('agent tool qianwen_skill {"action":"status"}');r['image_samples'].append({'job':v,'memory':q('agent status')})
  if v.get('state') not in ('running','submitted','pending','rendering','generating'):break
  time.sleep(2)
 r['image_result']=v;assert v.get('rendered'),v
 r['after']=q('agent status');r['passed']=True
except Exception as e:r['error']=repr(e);raise
finally:
 try:d.command('agent voice on')
 except Exception:pass
 d.save();d.close()
 receipts=[]
 for line in (a.out/'serial.log').read_text(encoding='utf8',errors='replace').splitlines():
  if line.startswith('@usage '):
   try:receipts.append(json.loads(line[7:]))
   except json.JSONDecodeError:r.setdefault('invalid_receipts',[]).append(line)
 r['receipts']=receipts
 (a.out/'report.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
