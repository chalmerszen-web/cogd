"""Capture a bounded acoustic fixture on the actual ES8311 microphone."""
from pathlib import Path
import sys,time,json,argparse
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/voice'),str(ROOT/'tools')]
from device import Device
from wave_play import play
from export_audio_clip import export_clip
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--source',type=Path,required=True)
p.add_argument('--gain',type=float,default=.35);p.add_argument('--port',default='COM3');a=p.parse_args()
d=Device(a.out,port=a.port);d.link.timeout=.02
result={'source':str(a.source),'playback_gain':a.gain,'cloud_upload':False}
try:
    d.command('agent voice off');d.command('agent audio capture 6000')
    end=time.monotonic()+10
    while time.monotonic()<end:
        s=d.command('agent audio status',query=True)
        if s['recording'] and s['capture_stage']==2:break
        time.sleep(.05)
    else:raise TimeoutError('Capture did not start')
    result['playback']=play(a.source,gain=a.gain)
    end=time.monotonic()+10
    while time.monotonic()<end:
        s=d.command('agent audio status',query=True)
        if not s['recording']:break
        time.sleep(.2)
    result['clip']=export_clip(d,a.out/'clip')
finally:
    d.save();d.close()
    (a.out/'report.json').write_text(json.dumps(result,indent=2),encoding='utf8')
print(json.dumps({'complete':result['clip']['complete'],'samples':result['clip']['samples']}))
