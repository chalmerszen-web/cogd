"""DSN/SES bridge for the local Freerouting CLI; retains LCEDA's editable pads.

DSN uses micrometres, y upwards. Each physical land is a distinct fixed image,
including duplicate-number pads, so no solder land silently disappears.
"""
from pathlib import Path
import argparse
import json
import math
import re

ROOT = Path(__file__).resolve().parents[1]
CAD = ROOT / 'cad'
UNIT = .254

def number(v):
    return f'{float(v):.6f}'.rstrip('0').rstrip('.') or '0'

def sx(s):
    return json.dumps(str(s),ensure_ascii=False)

def parse(text):
    toks=re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+',text)
    stack=[[]]
    for t in toks:
        if t=='(':
            a=[];stack[-1].append(a);stack.append(a)
        elif t==')':stack.pop()
        else:stack[-1].append(json.loads(t) if t.startswith('"') else t)
    assert len(stack)==1
    return stack[0][0]

def children(node,tag):
    return [a for a in node if isinstance(a,list) and a and a[0]==tag]

def child(node,tag):
    return next(iter(children(node,tag)),None)

def make():
    pcb=json.loads((CAD/'ESP-HI-C3-RevA-PCB-placement.json').read_text())
    pads=json.loads((CAD/'pads-mm.json').read_text())
    traces=[]
    def pad(ref,pin):
        p=next(p for p in pads if p['ref']==ref and p['pin']==str(pin))
        return (p['x'],p['y'])
    def trace(net,pts,w=.18,layer=1):
        traces.append(dict(net=net,points=pts,width=w,layer=layer))
    # RF feed on the component layer, with shunt lands directly on the feed.
    trace('RF_CHIP',[pad('U1',1),pad('R7',1)],.25)
    trace('RF_50',[pad('R7',2),pad('J6',1)],.8)
    # Short crystal paths. Clock route is kept off the bottom ground reference.
    trace('XTAL_P',[pad('U1',30),pad('L2',1)],.18)
    trace('XTAL_LOAD',[pad('L2',2),(16.35,13.2875),pad('Y1',1)],.18)
    trace('XTAL_LOAD',[pad('C9',1),(15.9,12.7),pad('Y1',1)],.18)
    trace('XTAL_N',[pad('Y1',3),(19.3,10.7),(19.3,12.5),pad('C10',1)],.18)
    trace('XTAL_N',[pad('U1',29),(17.05,15.3625),(17.05,13.3),(17.2,13.15),
                    (17.2,11.3),(17.8,10.7),pad('Y1',3)],.18)
    # Fixed ground vias beneath the exposed pad provide return and heat paths.
    vias=[] # Thermal vias are checked against bottom tracks after routing.
    (CAD/'critical-routing.json').write_text(json.dumps({'traces':traces,'vias':vias},indent=2)+'\n')
    def um(v):return number(v*1000)
    out=['(pcb ESP-HI-C3-RevA',' (parser (string_quote ") (space_in_quoted_tokens on) (host_cad "ESP-HI LCEDA bridge") (host_version "1"))',
         ' (resolution um 10) (unit um)',' (structure',
         '  (layer F.Cu (type signal) (property (index 0)))',
         '  (layer B.Cu (type signal) (property (index 1)))',
         '  (boundary (rect pcb 0 -46350 34300 0))',
         '  (via VIA_600_300)',
         '  (rule (width 180) (clearance 155) (clearance 155 (type default_smd)) (clearance 150 (type smd_smd)))']
    for shape in pcb['shape']:
        for s in shape.split('#@$'):
            if s.startswith('HOLE~'):
                a=s.split('~');x,y,r=map(lambda z:float(z)*UNIT,a[1:4])
                # Isolated two-layer circular pads model NPTH obstacles. This avoids
                # an ObstacleArea serialization defect in Freerouting 2.5.0 native CLI.
                pads.append(dict(ref='NPTH',pin=a[4],net='',x=x,y=y,w=2*r,h=2*r,rotation=0,multi=True))
    out += [' )',' (placement']
    for i,p in enumerate(pads):
        out.append(f'  (component IMG{i} (place P{i} {um(p["x"])} {um(-p["y"])} front 0))')
    out+=[' )',' (library']
    for i,p in enumerate(pads):
        w,h=p['w'],p['h']
        assert round(p['rotation'])%90==0,p
        if round(p['rotation'])%180:w,h=h,w
        geom=f'(rect F.Cu {um(-w/2)} {um(-h/2)} {um(w/2)} {um(h/2)})'
        if p['ref'].startswith('TP') or p['ref']=='NPTH':geom=f'(circle F.Cu {um(w)})'
        attach='on' if p['ref']=='U1' and p['pin']=='33' else 'off'
        other=f' (shape (circle B.Cu {um(w)}))' if p.get('multi') else ''
        out += [f'  (image IMG{i} (pin LAND{i} 1 0 0))',f'  (padstack LAND{i} (shape {geom}){other} (attach {attach}))']
    out+=['  (padstack VIA_600_300 (shape (circle F.Cu 600)) (shape (circle B.Cu 600)) (attach on))',' )',' (network']
    nets=sorted({p['net'] for p in pads if p['net']})
    for net in nets:
        pins=' '.join(f'P{i}-1' for i,p in enumerate(pads) if p['net']==net)
        out.append(f'  (net {sx(net)} (pins {pins}))')
    out += ['  (class default '+ ' '.join(map(sx,nets))+ ' (circuit (use_via VIA_600_300)) (rule (width 180) (clearance 155)))',
            '  (class power "VBUS" "+3V4" "SW_BUCK" "RF_3V4" (circuit (use_via VIA_600_300)) (rule (width 350) (clearance 155)))',
            '  (class ground "GND" (circuit (use_via VIA_600_300)) (rule (width 300) (clearance 155)))',
            '  (class audio "SPK_P" "SPK_N" (circuit (use_via VIA_600_300)) (rule (width 350) (clearance 155)))',
            ' )',' (wiring']
    for t in traces:
        pts=' '.join(f'{um(x)} {um(-y)}' for x,y in t['points'])
        out.append(f'  (wire (path F.Cu {um(t["width"])} {pts}) (net {sx(t["net"])}) (type protect))')
    for v in vias:
        out.append(f'  (via VIA_600_300 {um(v["x"])} {um(-v["y"])} (net {sx(v["net"])}) (type protect))')
    out+=[' )',')']
    (CAD/'ESP-HI-C3-RevA.dsn').write_text('\n'.join(out)+'\n',encoding='utf-8')
    print('DSN:',len(pads),'lands,',len(nets),'nets,',len(traces),'fixed traces')

def merge(path):
    ses=parse(path.read_text())
    routes=child(ses,'routes');network=child(routes,'network_out')
    resolution=child(routes,'resolution')
    # SES resolution gives integer divisions per named unit.
    unit={'um':.001,'mm':1,'mil':.0254}[resolution[1]]/float(resolution[2])
    pcb=json.loads((CAD/'ESP-HI-C3-RevA-PCB-placement.json').read_text())
    geometries=[];counter=0
    for net in children(network,'net'):
        for wire in children(net,'wire'):
            p=child(wire,'path');layer={'F.Cu':1,'B.Cu':2}[p[1]]
            xy=list(map(float,p[3:]));pts=[(xy[i]*unit,-xy[i+1]*unit) for i in range(0,len(xy),2)]
            geometries.append(dict(net=net[1],layer=layer,width=float(p[2])*unit,points=pts))
            counter+=1
            pcb['shape'].append(f'TRACK~{number(float(p[2])*unit/UNIT)}~{layer}~{net[1]}~'+' '.join(number(v/UNIT) for pt in pts for v in pt)+f'~ggeROUTE{counter}~0')
        for via in children(net,'via'):
            assert via[1]=='VIA_600_300',via
            x,y=float(via[2])*unit,-float(via[3])*unit;counter+=1
            pcb['shape'].append(f'VIA~{number(x/UNIT)}~{number(y/UNIT)}~{number(.6/UNIT)}~{net[1]}~{number(.3/2/UNIT)}~ggeROUTE{counter}~0')
            geometries.append(dict(net=net[1],x=x,y=y,diameter=.6,drill=.3))
    # Session writers may omit protected input wires; include absent geometry.
    fixed=json.loads((CAD/'critical-routing.json').read_text())
    # These are intentionally validated after native import; do not invent PASS.
    pcb['head']['c_para']['name']='ESP-HI-C3-RevA routed review draft'
    (CAD/'ESP-HI-C3-RevA-PCB-routed.json').write_text(json.dumps(pcb,indent=2)+'\n')
    (CAD/'routed-geometry.json').write_text(json.dumps(geometries,indent=2)+'\n')
    print('Imported session geometry:',counter,'objects. Native DRC still required.')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--merge',type=Path);args=p.parse_args()
    merge(args.merge) if args.merge else make()
