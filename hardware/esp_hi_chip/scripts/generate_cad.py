"""Emit editable LCEDA Standard JSON from checked connections and footprint data.

Coordinates in the intermediate model are mm; LCEDA Standard stores 10 mil units.
This creates a placement draft. It does NOT report native ERC/DRC or routing PASS.
"""
from pathlib import Path
import csv
import html
import json
import math
import re
import shutil
import uuid

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
OUT = ROOT/'cad'
LIB = ROOT/'libraries'
OUT.mkdir(exist_ok=True)
LIB.mkdir(exist_ok=True)
MODEL = json.loads((ROOT/'design-intent.json').read_text(encoding='utf-8'))
PARTS = MODEL['components']
BY_REF = {p['ref']:p for p in PARTS}
KICAD = REPO/'.local/kicad-footprints'
UNIT = .254
counter = 0

def ident(prefix='gge'):
    global counter
    counter += 1
    return prefix+str(counter)

def uid(name):
    return uuid.uuid5(uuid.NAMESPACE_URL,'esp-hi-chip-reva/'+name).hex

def fmt(v):
    return f'{float(v):.5f}'.rstrip('0').rstrip('.') or '0'

def mm(v):
    return fmt(v/UNIT)

def attrs(d):
    return '`'.join(str(x) for kv in d.items() for x in kv)

def sexpr(text):
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+',text)
    stack=[[]]
    for t in tokens:
        if t=='(':
            node=[];stack[-1].append(node);stack.append(node)
        elif t==')':stack.pop()
        else:stack[-1].append(json.loads(t) if t.startswith('"') else t)
    return stack[0][0]

def sub(node,key,default=None):
    return next((x[1:] for x in node if isinstance(x,list) and x and x[0]==key),default)

def read_footprint(path):
    node=sexpr(path.read_text(encoding='utf-8'))
    pads=[];lines=[];paste_apertures=[]
    for n in node[1:]:
        if not isinstance(n,list):continue
        if n[0]=='pad':
            xy=sub(n,'at');sz=sub(n,'size');layers=sub(n,'layers',[])
            if 'F.Cu' not in layers and n[2]!='np_thru_hole':
                if 'F.Paste' in layers:
                    paste_apertures.append([float(xy[0]),float(xy[1]),float(sz[0]),float(sz[1])])
                continue
            pads.append(dict(number=n[1],shape=n[3],x=float(xy[0]),y=float(xy[1]),
                w=float(sz[0]),h=float(sz[1]),rot=float(xy[2]) if len(xy)>2 else 0,
                hole=float(sub(n,'drill',['0'])[0]),paste='F.Paste' in layers,
                mask='F.Mask' in layers,kind=n[2]))
        elif n[0]=='fp_line':
            a=sub(n,'start');b=sub(n,'end');layer=sub(n,'layer',[''])[0]
            if layer in ('F.CrtYd','F.Fab','F.SilkS'):
                lines.append(dict(a=list(map(float,a)),b=list(map(float,b)),layer=layer))
    # Copper bounds are included even if a source has no courtyard.
    box=[]
    for p in pads:
        w,h=p['w'],p['h']
        if int(p['rot'])%180:w,h=h,w
        box += [(p['x']-w/2,p['y']-h/2),(p['x']+w/2,p['y']+h/2)]
    for l in lines:
        if l['layer']=='F.CrtYd':box += [l['a'],l['b']]
    return dict(name=node[1],pads=pads,lines=lines,paste_apertures=paste_apertures,
        bbox=[min(x[0] for x in box),min(x[1] for x in box),max(x[0] for x in box),max(x[1] for x in box)],
        source=str(path.relative_to(ROOT)))

def land(num,x,y,w,h,shape='rect'):
    return dict(number=str(num),x=x,y=y,w=w,h=h,rot=0,shape=shape,hole=0,
                paste=True,mask=True,kind='smd')

def custom(name,pads,bbox,note):
    x1,y1,x2,y2=bbox
    lines=[dict(a=a,b=b,layer='F.Fab') for a,b in [
        ([x1,y1],[x2,y1]),([x2,y1],[x2,y2]),([x2,y2],[x1,y2]),([x1,y2],[x1,y1])]]
    return dict(name=name,pads=pads,lines=lines,bbox=bbox,source=note)

sources = {
 'QFN32_5x5_EP':'Package_DFN_QFN.pretty/QFN-32-1EP_5x5mm_P0.5mm_EP3.45x3.45mm.kicad_mod',
 'TSOT26':'Package_TO_SOT_SMD.pretty/TSOT-23-6.kicad_mod',
 'SOT23-6':'Package_TO_SOT_SMD.pretty/SOT-23-6.kicad_mod',
 'SOT23-5':'Package_TO_SOT_SMD.pretty/SOT-23-5.kicad_mod',
 'VSSOP8':'Package_SO.pretty/VSSOP-8_2.3x2mm_P0.5mm.kicad_mod',
 'MSOP8':'Package_SO.pretty/MSOP-8_3x3mm_P0.65mm.kicad_mod',
 'SOD323':'Diode_SMD.pretty/D_SOD-323.kicad_mod',
 'JST_SH_2_RA':'Connector_JST.pretty/JST_SH_SM02B-SRSS-TB_1x02-1MP_P1.00mm_Horizontal.kicad_mod',
 'JST_SH_4_RA':'Connector_JST.pretty/JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal.kicad_mod',
 'U.FL_SMT':'Connector_Coaxial.pretty/U.FL_Hirose_U.FL-R-SMT-1_Vertical.kicad_mod',
 'SMD_Tactile_2P':'Button_Switch_SMD.pretty/SW_SPST_PTS810.kicad_mod',
 'Inductor_4x4':'Inductor_SMD.pretty/L_Coilcraft_XxL4030.kicad_mod',
}
for imperial,metric in [('0402','1005'),('0603','1608'),('0805','2012'),('1210','3225')]:
    sources[imperial]=f'Resistor_SMD.pretty/R_{imperial}_{metric}Metric.kicad_mod'
fps={}
for key,src in sources.items():
    dst=LIB/Path(src).name
    shutil.copy2(KICAD/src,dst)
    fps[key]=read_footprint(dst)
    fps[key]['upstream']=src
shutil.copy2(KICAD/'LICENSE.md',LIB/'KICAD-LICENSE.md')

# Datasheet-specific footprints; values are dimensions, not pixels read from photos.
fps['Crystal_2520_4P']=custom('EPSON_FA20H',[
 land(1,-.85,.7,1.2,1.1),land(2,.85,.7,1.2,1.1),
 land(3,.85,-.7,1.2,1.1),land(4,-.85,-.7,1.2,1.1)],[-1.7,-1.5,1.7,1.5],
 'Epson FA-20H brief sheet p1 recommended land pattern')
fps['LED_2020_4P']=custom('WS2812B_2020_V6',[
 land(1,.915,.55,.7,.7),land(2,.915,-.55,.7,.7),
 land(3,-.915,-.55,.7,.7),land(4,-.915,.55,.7,.7)],[-1.45,-1.2,1.45,1.2],
 'Worldsemi WS2812B-2020-V6 p2, component-side pin arrangement')
usb=[]
for num,x,w in [('A1/B12',-3.2,.6),('A4/B9',-2.4,.6),('B8',-1.75,.3),
 ('A5',-1.25,.3),('B7',-.75,.3),('A6',-.25,.3),('A7',.25,.3),
 ('B6',.75,.3),('A8',1.25,.3),('B5',1.75,.3),('A9/B4',2.4,.6),('A12/B1',3.2,.6)]:
    # Trim only the two ground lands by 0.035 mm at each end so the
    # locating NPTH has >=0.20 mm copper clearance. Contact centers stay put.
    usb.append(land(num,x,-7.355,w,1.08 if num in ('A1/B12','A12/B1') else 1.15))
for i,(x,y) in enumerate([(-5.11,-6.78),(5.11,-6.78),(-5.11,-2.85),(5.11,-2.85)],1):
    usb.append(land('S'+str(i),x,y,2.18,2))
for x in (-2.89,2.89):
    p=land('',x,-6.28,.65,.65,'circle');p.update(kind='np_thru_hole',hole=.65,paste=False,mask=False);usb.append(p)
fps['USB4110']=custom('GCT_USB4110_GF_A',usb,[-6.45,-8.2,6.45,.2],
 'GCT USB4110 B4 p1 layout; origin at mouth; ground lands shortened 1.15 to 1.08 mm for >=0.20 mm NPTH clearance')
fpc=[land(i+1,-5.75+.5*i,-.4,.3,.8) for i in range(24)]
# L-shaped mounting lands, expressed as two overlapping same-net rectangles.
for side in (-1,1):
    fpc += [land('MP',side*8.225,1.1,2.35,2.2),land('MP',side*7.675,2.625,1.25,.85)]
fps['Molex_52435_24P']=custom('Molex_52435_2471',fpc,[-9.65,-1.2,9.65,5.9],
 'Molex SD-52435-016 rev D, pages 1-2, Farnell 2174151.pdf; includes open actuator envelope')
fps['TestPad_1mm']=custom('TestPad_1mm',[land(1,0,0,1,1,'circle')],[-.7,-.7,.7,.7],
 '1.0mm SMT probe contact; no paste aperture')
fps['TestPad_1mm']['pads'][0]['paste']=False
(LIB/'footprints-mm.json').write_text(json.dumps(fps,indent=2)+'\n',encoding='utf-8')

# Placement anchors: every part remains on the component face; y grows downward.
poses={
 'J1':(17.15,-1,180),'J2':(17.15,30.5,0),
 'J3':(3.15,25.9,90),'J4':(31.15,34,-90),'J5':(3.15,32.8,90),
 'J6':(8.1,16.25,180),'U1':(17.2,18,0),'Y1':(17.2,11.4,0),
 'R7':(12.15,16.25,180),'C12':(13.6,15.8,270),'C13':(10.7,15.8,270),
 'C8':(12.1,17.6,180),
 'L2':(16.45,13.9,270),'C9':(14.3,12.7,180),'C10':(20,12.5,0),
 'C4':(13.1,14.1,180),'C5':(15,14.1,0),
 'U2':(28.2,8.7,0),'L3':(28.5,12.8,90),'U3':(30.7,27.7,90),
 'U4':(8.5,27.4,0),'U5':(19.2,27.5,0),'U6':(25.8,27.4,0),
 'D1':(21.3,9.0,0),'D2':(22.6,27.5,90),
 'SW1':(30.9,22,0),'SW2':(30.9,18,0),'SW3':(3.3,20.8,0),
 'R14':(5,8,90),
 'LED1':(2.6,2.8,0),'LED2':(7.5,2.8,0),'LED3':(26.8,2.8,0),'LED4':(31.7,2.8,0),
}
keepouts=[(1.5,37,32.8,46.35)]
# Reserve the hand-routed RF/crystal corridors before placing other passives.
critical_corridors=[]
for width,pts in [(.8,[(9.15,16.25),(14.7625,16.25)]),
 (.18,[(16.45,15.5625),(16.45,13.3875),(16.35,12.1)]),
 (.18,[(14.81,12.7),(15.9,12.7),(16.35,12.1)]),
 (.18,[(18.05,10.7),(19.3,10.7),(19.3,12.5),(19.49,12.5)]),
 (.18,[(16.95,15.5625),(17.05,15.3625),(17.05,13.3),(17.2,13.15),(17.2,11.3),(17.8,10.7),(18.05,10.7)])]:
    margin=width/2+.17
    for (x1,y1),(x2,y2) in zip(pts,pts[1:]):
        critical_corridors.append((min(x1,x2)-margin,min(y1,y2)-margin,max(x1,x2)+margin,max(y1,y2)+margin))
escape_corridors=[(14,21.15,20.4,22.55),(13.1,18.15,14.1,20.8),(17.35,13.5,20.3,15.1)]

def transform(x,y,px,py,angle):
    a=math.radians(angle);return px+x*math.cos(a)-y*math.sin(a),py+x*math.sin(a)+y*math.cos(a)

def bounds(fp,pose):
    a,b,c,d=fp['bbox'];pts=[transform(x,y,*pose) for x,y in [(a,b),(a,d),(c,b),(c,d)]]
    return min(x for x,y in pts),min(y for x,y in pts),max(x for x,y in pts),max(y for x,y in pts)

def overlap(a,b,gap=0):
    return not(a[2]+gap<=b[0] or b[2]+gap<=a[0] or a[3]+gap<=b[1] or b[3]+gap<=a[1])

if '--keep-placement' in __import__('sys').argv:
    previous=json.loads((OUT/'placement.json').read_text())
    previous.update({r:poses[r] for r in ('J3','J4','J5')})
    poses=previous
placed=[]
for p in PARTS:
    if p['ref'] in poses:
        pose=poses[p['ref']];placed.append((p['ref'],bounds(fps[p['footprint']],pose)))
for p in sorted([p for p in PARTS if p['ref'] not in poses],key=lambda p:-math.prod([fps[p['footprint']]['bbox'][i+2]-fps[p['footprint']]['bbox'][i] for i in (0,1)])):
    ax,ay=p['pcb_mm']
    if p['block']=='mic':ax=max(5,ax-1);ay=min(29.5,ay-1.5)
    if p['block']=='display':ay=min(29.6,ay-5)
    if p['ref']=='L2':ax,ay=16.7,14.1
    if p['ref']=='C9':ax,ay=14.2,12.8
    if p['ref']=='C10':ax,ay=18.3,12.8
    if p['ref']=='R7':ax,ay=12.2,16.25
    if p['ref']=='C12':ax,ay=13.4,16.5
    if p['ref']=='C13':ax,ay=11,16.5
    options=[]
    for angle in (0,90):
        for ix in range(5,133,2):
            for iy in range(5,145,2):
                x,y=ix*.25,iy*.25
                score=(x-ax)**2+(y-ay)**2+(angle==90)*.05
                options.append((score,(x,y,angle)))
    for score,pose in sorted(options):
        b=bounds(fps[p['footprint']],pose)
        if b[0]<.3 or b[1]<.3 or b[2]>34 or b[3]>46.05:continue
        if any(overlap(b,k,.1) for k in keepouts):continue
        if any(overlap(b,k) for k in critical_corridors):continue
        if any(overlap(b,k) for k in escape_corridors):continue
        if any(overlap(b,k,.1) for ref,k in placed):continue
        poses[p['ref']]=pose;placed.append((p['ref'],b));break
    else:raise RuntimeError('No legal placement for '+p['ref'])

placement_errors=[]
for i,(r,a) in enumerate(placed):
    for s,b in placed[i+1:]:
        if overlap(a,b):placement_errors.append(f'Courtyard overlap {r}/{s}')
    if any(overlap(a,k) for k in keepouts):placement_errors.append(f'FPC keepout overlap {r}')
(OUT/'placement.json').write_text(json.dumps({r:list(pos) for r,pos in poses.items()},indent=2)+'\n',encoding='utf-8')

pin_names={
 'U1':dict(enumerate(['LNA_IN','VDD3P3','VDD3P3','GPIO0','GPIO1','GPIO2/ADC1_CH2','CHIP_EN','GPIO3','GPIO4','GPIO5','VDD3P3_RTC','GPIO6','GPIO7','GPIO8/STRAP','GPIO9/BOOT','GPIO10','VDD3P3_CPU','VDD_SPI','NC','NC','NC','NC','NC','NC','GPIO18/USB_D-','GPIO19/USB_D+','GPIO20/RX','GPIO21/TX','XTAL_N','XTAL_P','VDDA','VDDA','EP/GND'],1)),
 'U2':{1:'FB',2:'EN',3:'VIN',4:'GND',5:'SW',6:'BST'},
 'U3':{1:'CTRL',2:'BYPASS',3:'INP',4:'INN',5:'VON',6:'VCC',7:'GND',8:'VOP'},
 'U4':{1:'IN+',2:'GND',3:'IN-',4:'OUT',5:'VCC'},
 'U5':{1:'1A',2:'3Y',3:'2A',4:'GND',5:'2Y',6:'3A',7:'1Y',8:'VCC'},
 'U6':{1:'IN',2:'GND',3:'EN',4:'NC',5:'OUT'},
}

def stxt(mark,x,y,value,size=9,anchor='start',color='#17324d',visible=1):
    return f'T~{mark}~{fmt(x)}~{fmt(y)}~0~{color}~Arial~{size}pt~~~~comment~{value}~{visible}~{anchor}~{ident()}~0~'

def spin(num,name,x,y,side,showname=True):
    s=1 if side=='left' else -1;end=x+s*20;rot=180 if side=='left' else 0
    return (f'P~show~0~{num}~{fmt(x)}~{fmt(y)}~{rot}~{ident()}~0^^{fmt(x)}~{fmt(y)}'
        f'^^M {fmt(x)} {fmt(y)} h {s*20}~#880000'
        f'^^{int(showname)}~{fmt(end+s*3)}~{fmt(y+3)}~0~{name}~'+('start' if s==1 else 'end')+f'~Arial~7pt~#23405f'
        f'^^1~{fmt(x+s*10)}~{fmt(y-4)}~0~{num}~middle~Arial~6pt~#555555'
        f'^^0~{fmt(end)}~{fmt(y)}^^0~M {fmt(end)} {fmt(y)}')

def draw_symbol(p,cx,top):
    pins=list(p['pins']);count=len(pins);passive=count==2 and p['ref'][0] in 'RCLD'
    children=[];connections=[]
    if passive:
        cy=top+45;w=40;h=22
        locations=[(pins[0],cx-40,cy,'left'),(pins[1],cx+40,cy,'right')]
        if p['ref'].startswith('C'):
            for x in (cx-4,cx+4):children.append(f'PL~{x} {cy-12} {x} {cy+12}~#880000~1~0~none~{ident()}~0')
            children += [f'PL~{cx-20} {cy} {cx-4} {cy}~#880000~1~0~none~{ident()}~0',f'PL~{cx+4} {cy} {cx+20} {cy}~#880000~1~0~none~{ident()}~0']
        else:children.append(f'R~{cx-20}~{cy-9}~~~40~18~#880000~1~0~none~{ident()}~0~')
        labeltop=cy-29
    else:
        n=math.ceil(count/2);h=max(60,n*20+20);cy=top+50+h/2;w=230
        children.append(f'R~{cx-w/2}~{cy-h/2}~~~{w}~{h}~#880000~1~0~#fbfdff~{ident()}~0~')
        locations=[]
        for i,num in enumerate(pins):
            side='left' if i<n else 'right';row=i if i<n else i-n
            locations.append((num,cx+(-w/2-20 if side=='left' else w/2+20),cy-h/2+20+row*20,side))
        labeltop=top+25
    children += [stxt('P',cx,labeltop,p['ref'],10,'middle'),stxt('N',cx,labeltop+15,p['value'],8,'middle')]
    for num,x,y,side in locations:
        n=pin_names.get(p['ref'],{}).get(int(num) if num.isdigit() else num,num)
        if p['ref'].startswith('LED'):n={'1':'DO','2':'GND','3':'DI','4':'VDD'}[num]
        children.append(spin(num,n,x,y,side,not passive))
        net=p['pins'][num];sgn=-1 if side=='left' else 1;ex=x+sgn*25
        if net:
            connections += [f'W~{fmt(x)} {fmt(y)} {fmt(ex)} {fmt(y)}~#008800~1~0~none~{ident()}~0',
                f'N~{fmt(ex)}~{fmt(y)}~0~#006030~{net}~{ident()}~'+('end' if sgn<0 else 'start')+f'~{fmt(ex)}~{fmt(y-4)}~Arial~7pt~0']
        else:connections.append(f'O~{fmt(x)}~{fmt(y)}~{ident()}~M {fmt(x-3)} {fmt(y-3)} L {fmt(x+3)} {fmt(y+3)} M {fmt(x-3)} {fmt(y+3)} L {fmt(x+3)} {fmt(y-3)}~#008800~0')
    a=attrs({'package':fps[p['footprint']]['name'],'Manufacturer Part':p['mpn'],'spicePre':re.sub(r'\d','',p['ref']),
              'name':p['value'],'Contributor':'ESP-HI chip project','BOM':'yes' if p['fitted'] else 'no'})
    header=f'LIB~{cx}~{fmt(cy)}~{a}~0~0~ggeSCH{p["ref"]}~{uid("fp/"+p["footprint"])}~{uid("sym/"+p["ref"])}~0~ggePCB{p["ref"]}~yes~yes'
    return [header+'#@$'+'#@$'.join(children)]+connections, (90 if passive else h+90)

sheets=[]
titles={'core':'01 MCU, crystal and reset','rf':'02 External antenna and matching','power':'03 USB-C and regulated supply',
 'display':'04 128x160 SPI display','speaker':'05 PDM reconstruction and speaker','mic':'06 Electret microphone and ADC','io':'07 Buttons, RGB and external IO'}
for block,title in titles.items():
    shapes=[stxt('L',40,35,'ESP-HI C3 CHIP REV A / '+title,17),stxt('L',40,58,'DRAFT - native ERC and physical verification pending. GPIO identities retained.',9)]
    heights=[100,100,100]
    group=sorted([p for p in PARTS if p['block']==block],key=lambda p:-len(p['pins']))
    for p in group:
        col=min(range(3),key=lambda i:heights[i]);cx=310+590*col
        s,h=draw_symbol(p,cx,heights[col]);shapes += s;heights[col]+=h+30
    H=max(heights)+40
    data={'head':{'docType':'1','editorVersion':'6.5.51','newgId':True,'c_para':{'Prefix Start':'1'},'c_spiceCmd':'null','hasIdFlag':True,'uuid':uid(block),'x':'0','y':'0'},
       'canvas':'CA~1800~1200~#FFFFFF~yes~#CCCCCC~5~1800~1200~line~5~pixel~5~0~0',
       'shape':shapes,'BBox':{'x':0,'y':0,'width':1800,'height':H},'colors':{}}
    sheets.append({'docType':'1','title':title,'description':'Rev A electrical draft','dataStr':data})
project={'editorVersion':'6.5.51','docType':'5','title':MODEL['name'],'description':'Seven-sheet editable schematic. Not released for manufacture.','colors':{},'schematics':sheets}
(OUT/'ESP-HI-C3-RevA-schematic.json').write_text(json.dumps(project,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def track(points,layer=1,net='',width=.15):
    return f'TRACK~{mm(width)}~{layer}~{net}~'+ ' '.join(mm(v) for p in points for v in p)+f'~{ident()}~0'

def pcbtext(kind,x,y,text,layer=3,size=.7,display=''):
    return f'TEXT~{kind}~{mm(x)}~{mm(y)}~{mm(.10)}~0~~{layer}~~{mm(size)}~{text}~~{display}~{ident()}~~0~'

pcbs=[];allpads=[]
for p in PARTS:
    fp=fps[p['footprint']];pose=poses[p['ref']];px,py,angle=pose;children=[]
    numbers={a['number'] for a in fp['pads'] if a['number']}
    assert numbers==set(p['pins']), (p['ref'],numbers ^ set(p['pins']))
    for a in fp['pads']:
        x,y=transform(a['x'],a['y'],*pose)
        # Standalone HOLE stores radius (verified against native DRC), unlike PAD's drill field.
        if a['kind']=='np_thru_hole':children.append(f'HOLE~{mm(x)}~{mm(y)}~{mm(a["hole"]/2)}~{ident()}~0');continue
        net=p['pins'][a['number']] or ''
        shape='ELLIPSE' if a['shape']=='circle' else 'OVAL' if a['shape']=='oval' else 'RECT'
        # LCEDA rotation has the opposite sign to our mm component-side coordinate rotation.
        rot=-(a['rot']+angle)
        children.append(f'PAD~{shape}~{mm(x)}~{mm(y)}~{mm(a["w"])}~{mm(a["h"])}~1~{net}~{a["number"]}~0~~{fmt(rot)}~{ident()}~0~~Y~0~'+('0' if a['paste'] else '-100')+'~0.1~')
        allpads.append(dict(ref=p['ref'],pin=a['number'],net=net,x=x,y=y,w=a['w'],h=a['h'],rotation=a['rot']+angle))
    for l in fp['lines']:
        lay=3 if l['layer']=='F.SilkS' else 12
        children.append(track([transform(*l['a'],*pose),transform(*l['b'],*pose)],lay,width=.1))
    for x,y,w,h in fp.get('paste_apertures',[]):
        corners=[transform(x+dx*w/2,y+dy*h/2,*pose) for dx,dy in [(-1,-1),(1,-1),(1,1),(-1,1)]]
        path='M '+' L '.join(f'{mm(x)} {mm(y)}' for x,y in corners)+' Z'
        children.append(f'SOLIDREGION~5~~{path}~solid~{ident()}~~~~0')
    b=bounds(fp,pose);children += [pcbtext('P',px,b[1]-.2,p['ref']),pcbtext('N',px,b[3]+.7,p['value'],display='none')]
    a=attrs({'package':fp['name'],'Manufacturer Part':p['mpn'],'spicePre':re.sub(r'\d','',p['ref']),'name':p['value']})
    pcbs.append(f'LIB~{mm(px)}~{mm(py)}~{a}~{fmt(-angle)}~0~ggePCB{p["ref"]}~1~{uid("fp/"+p["footprint"])}~1791633600~0#@$'+'#@$'.join(children))
pcbs.append(track([(0,0),(34.3,0),(34.3,46.35),(0,46.35),(0,0)],10,width=.05))
for k in keepouts:
    x1,y1,x2,y2=k
    pcbs.append(track([(x1,y1),(x2,y1),(x2,y2),(x1,y2),(x1,y1)],12,width=.15))
pcbs += [pcbtext('L',4.5,42,'FPC FOLD - NO COMPONENTS',12,size=.7),pcbtext('L',4.5,44,'DRAFT / NOT FOR FABRICATION',12,size=.65)]
template=json.loads((REPO/'.local/eda-standard-format/example/easyeda-std-pcb-file-format-example.json').read_text())
layers=[l for l in template['layers'] if not l.split('~')[0].isdigit() or int(l.split('~')[0])<=20]
pcb={'head':{'docType':'3','editorVersion':'6.5.51','newgId':True,'c_para':{'name':MODEL['name'],'thickness':'1.0'},'x':0,'y':0,'hasIdFlag':True},
 'canvas':'CA~1000~1000~#000000~yes~#FFFFFF~.5~1000~1000~line~0.1~mm~0.6~45~~0.1~0~0~1~yes',
 'shape':pcbs,'layers':layers,'objects':template['objects'],
 'BBox':{'x':0,'y':0,'width':34.3/UNIT,'height':46.35/UNIT},'preference':{'hideFootprints':'','hideNets':''},
 'DRCRULE':{'Default':{'trackWidth':.15/UNIT,'clearance':.15/UNIT,'viaHoleDiameter':.6/UNIT,'viaHoleD':.3/UNIT},'isRealtime':True,'checkObjectToCopperarea':True,'showDRCRangeLine':True},
 'netColors':{},'routerRule':{'unit':'mm','trackWidth':.15,'trackClearance':.15,'viaHoleD':.3,'viaDiameter':.6,'routerLayers':[1,2],'smdClearance':.15,'specialNets':[],'nets':sorted(set(a['net'] for a in allpads if a['net'])),'padsCount':len(allpads),'skipNets':[],'realtime':True}}
(OUT/'ESP-HI-C3-RevA-PCB-placement.json').write_text(json.dumps(pcb,indent=2)+'\n',encoding='utf-8')
(OUT/'pads-mm.json').write_text(json.dumps(allpads,indent=2)+'\n',encoding='utf-8')
report={'status':'PLACEMENT_DRAFT_UNROUTED','components':len(PARTS),'schematic_sheets':len(sheets),'copper_layers':[1,2],
 'top_components':len(PARTS),'bottom_components':0,'pad_mapping':'PASS: all model pins have matching footprint pads',
 'courtyard_errors':placement_errors,'native_erc':'NOT RUN','native_drc':'NOT RUN','routing':'NOT RUN',
 'release_blocks':['Molex current sales drawing and physical FPC fold verification','Native schematic/PCB import, connectivity, ERC and DRC','All-net routing with ground plane, RF and USB review','Prototype power, RF, crystal, audio and LCD validation']}
(ROOT/'evidence/cad-generation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report))
