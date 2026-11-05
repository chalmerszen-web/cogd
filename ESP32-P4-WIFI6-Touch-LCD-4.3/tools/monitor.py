"""Bounded UART capture with an optional hardware reset; no credentials."""
from pathlib import Path
import sys,time,argparse
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from serial_link import connect
p=argparse.ArgumentParser();p.add_argument('--port',default='COM3');p.add_argument('--seconds',type=int,default=30)
p.add_argument('--out',type=Path,required=True);p.add_argument('--reset',action='store_true');a=p.parse_args()
a.out.parent.mkdir(parents=True,exist_ok=True)
with connect(a.port) as link,a.out.open('ab') as f:
    if a.reset:
        link.rts=True;time.sleep(.15);link.rts=False
    end=time.monotonic()+a.seconds
    while time.monotonic()<end:
        data=link.read(8192)
        if data:f.write(data);f.flush()
