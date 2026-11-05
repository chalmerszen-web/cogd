"""Recover PDF wire topology and labels, retaining all geometric evidence."""
from pathlib import Path
import sys,json,math,collections,hashlib,re
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parent/'.local/pcb-python'))
import pymupdf
doc=pymupdf.open(ROOT/'hardware/references/original.pdf');page=doc[0];draw=page.get_drawings()
pins=json.loads((ROOT/'evidence/pdf-pin-geometry.json').read_text())
assert not pins['no_match'] and not pins['duplicate_segments']
lines=[];dots=[];red=[]
for d in draw:
    c=d['color'];f=d['fill']
    blue=c and c[2]>.4 and c[2]<.65 and c[0]<.12 and c[1]<.12
    if f and .4<f[2]<.65 and f[0]<.12 and f[1]<.12 and d['rect'].width<1.5 and d['rect'].height<1.5:
        dots.append(list((d['rect'].tl+d['rect'].br)/2))
    for item in d['items']:
        if item[0]!='l':continue
        a,b=map(list,item[1:]);length=math.dist(a,b)
        if c and c[0]>.4 and c[1]<.12 and c[2]<.12:red.append((a,b,length))
        if blue:lines.append({'a':a,'b':b,'kind':'wire'})
for r in pins['rows']:
    s=pins['segments'][r['candidates'][0][1]]
    r['line']=len(lines);lines.append(dict(a=s['a'],b=s['b'],kind='pin',pin=r['pin']))
parent=list(range(len(lines)))
def root(i):
    while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
    return i
def union(a,b):
    a,b=root(a),root(b)
    if a!=b:parent[b]=a
def distance(p,a,b):
    dx=b[0]-a[0];dy=b[1]-a[1];q=dx*dx+dy*dy
    t=max(0,min(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/q)) if q else 0
    return math.hypot(p[0]-a[0]-t*dx,p[1]-a[1]-t*dy)
horizontal=collections.defaultdict(list);vertical=collections.defaultdict(list)
for i,s in enumerate(lines):
    a,b=s['a'],s['b']
    if abs(a[1]-b[1])<.03:horizontal[round(a[1]*20)].append(i)
    elif abs(a[0]-b[0])<.03:vertical[round(a[0]*20)].append(i)
def touches(p):
    possible=[]
    for delta in [-1,0,1]:possible+=horizontal.get(round(p[1]*20)+delta,[])+vertical.get(round(p[0]*20)+delta,[])
    return [i for i in set(possible) if distance(p,lines[i]['a'],lines[i]['b'])<.035]
for i,s in enumerate(lines):
    for p in [s['a'],s['b']]:
        for j in touches(p):union(i,j)
for p in dots:
    hit=touches(p)
    for j in hit[1:]:union(hit[0],j)
def has_red(a,b):
    return any(math.dist(a,x)<.035 and math.dist(b,y)<.035 or math.dist(a,y)<.035 and math.dist(b,x)<.035 for x,y,_ in red)
ports=[]
for a,b,length in red:
    small=abs(length-2.253)<.04
    if not small and abs(length-4.506)>.04:continue
    axis=(abs(a[1]-b[1])<.03,abs(a[0]-b[0])<.03)
    if not any(axis):continue
    c=[(a[k]+b[k])/2 for k in [0,1]];v=[1,0] if axis[0] else [0,1]
    for sign in [-1,1]:
        n=[-v[1]*sign,v[0]*sign]
        def point(along,normal):return [c[k]+v[k]*along+n[k]*normal for k in [0,1]]
        bars=has_red(point(-1.577,.675),point(1.577,.675)) and has_red(point(-.901,1.35),point(.901,1.35))
        triangle=has_red(a,point(0,2.25)) and has_red(b,point(0,2.25))
        if has_red(c,point(0,-2.25)) and (bars or triangle or small):
            ports.append({'anchor':point(0,-2.25),'label_center':point(0,5.4 if bars or triangle else .93),'base':c,'normal':n,'kind':'bars' if bars else 'triangle' if triangle else 'power_bar'})
spans=[s for block in page.get_text('dict')['blocks'] if 'lines'in block for l in block['lines'] for s in l['spans'] if s['alpha']]
labels=[];unbound=[]
for s in spans:
    color=s['color'];text=s['text'].strip()
    if not ((color>>16)&255>90 and color&255<35) or len(text)<2 or s['size']>3.5:continue
    box=pymupdf.Rect(s['bbox']);center=list((box.tl+box.br)/2)
    if text in ['GND','AGND','DGND'] or re.fullmatch(r'(?:ESP_)?(?:A?\dV\d|\dV|LDO_VO\d)|VRTC|VBACKUP|VBUS|VSYS|PVDD|DVDD|VBAT',text):
        if not ports:unbound.append(dict(text=text,xy=center));continue
        def port_cost(p):
            if text in ['GND','AGND','DGND']:
                return math.dist(p['label_center'],center) if p['kind']!='power_bar' else 1e6
            if p['kind']=='bars':return 1e6
            if p['kind']=='triangle':return math.dist(p['label_center'],center)
            x,y=p['base'];n=p['normal']
            if sum((center[k]-p['base'][k])*n[k] for k in [0,1])<=0:return 1e6
            return math.hypot(max(box.x0-x,0,x-box.x1),max(box.y0-y,0,y-box.y1))
        port=min(ports,key=port_cost);cost=port_cost(port)
        candidates=touches(port['anchor']) if cost<(5 if text in ['GND','AGND','DGND'] else 2) else []
        if candidates:labels.append(dict(text=text,line=candidates[0],cost=cost,xy=center,method='power_port'));continue
        if text in ['GND','AGND','DGND']:unbound.append(dict(text=text,xy=center,cost=cost));continue
    # Named net text sits just above its wire. Do not include pin/body graphics.
    ranked=[]
    for i,d in enumerate(lines):
        if d['kind']!='wire':continue
        a,b=d['a'],d['b']
        # Minimum distance from the visible label box to a wire, with a mild
        # preference for the label's left/bottom electrical anchor.
        points=[(box.x0,box.y1),(box.x1,box.y1),(center[0],box.y1)]
        cost=min(distance(p,a,b) for p in points)
        if abs(a[1]-b[1])<.03 and a[1]<box.y0:cost+=1
        ranked.append((cost,i))
    ranked.sort();cost,i=ranked[0]
    if cost<4:labels.append(dict(text=text,line=i,cost=cost,xy=center,method='wire_label'))
    else:unbound.append(dict(text=text,xy=center,cost=cost))
# Equal global labels merge distant conductor islands. Aliases are retained.
islands=collections.defaultdict(lambda:{'labels':[],'pins':[]})
for r in pins['rows']:islands[root(r['line'])]['pins'].append(r['pin'])
for label in labels:islands[root(label['line'])]['labels'].append(label)
(ROOT/'evidence/conductor-islands.json').write_text(json.dumps(list(islands.values()),ensure_ascii=False,indent=2),encoding='utf8')
named={}
for label in labels:
    if label['text'] in named:union(label['line'],named[label['text']])
    else:named[label['text']]=label['line']
groups=collections.defaultdict(lambda:{'pins':[],'labels':set(),'lines':[]})
for i,s in enumerate(lines):groups[root(i)]['lines'].append(i)
for r in pins['rows']:groups[root(r['line'])]['pins'].append(r['pin'])
for label in labels:groups[root(label['line'])]['labels'].add(label['text'])
netrows=[]
for g in groups.values():
    if not g['pins']:continue
    g['labels']=sorted(g['labels']);g['pins'].sort();g['name']=g['labels'][0] if g['labels'] else 'Net'+g['pins'][0].replace('-','_')
    netrows.append(g)
netrows.sort(key=lambda g:g['name'])
report={'source_sha256':hashlib.sha256((ROOT/'hardware/references/original.pdf').read_bytes()).hexdigest(),
        'pin_count':len(pins['rows']),'wire_segments':sum(s['kind']=='wire' for s in lines),'junctions':len(dots),
        'ground_ports':len(ports),'labels':labels,'unbound_labels':unbound,'nets':netrows,'segments':lines,'ports':ports,
        'verification':'GEOMETRY RECONSTRUCTION; review required before hardware design'}
(ROOT/'evidence/reconstructed-nets.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'pins':len(pins['rows']),'nets':len(netrows),'ground_ports':len(ports),'bound_labels':len(labels),'unbound_labels':unbound,
 'multiple_names':[{'names':g['labels'],'pin_count':len(g['pins'])} for g in netrows if len(g['labels'])>1]},ensure_ascii=False))
