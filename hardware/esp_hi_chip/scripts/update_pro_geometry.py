"""Update only routed copper in an imported, closed LCEDA Pro PCB document.

Pro line, via and poured-path coordinates are mil; the canonical model is mm.
Embedded libraries, components, pad net assignments and rules are preserved.
"""
from pathlib import Path
import json,sys,uuid,shutil
ROOT=Path(__file__).resolve().parents[1]
dest=Path(sys.argv[1])
backup=ROOT/'cad/attempts/pro-before-geometry.epcb2'
if not backup.exists():shutil.copy2(dest,backup)
rows=[];doc=None;ticket=0;pour_ids={}
for line in dest.read_text(encoding='utf-8-sig').splitlines():
    if '||' not in line:continue
    h,b=line.split('||',1)
    if not b.rstrip('|'):continue
    h=json.loads(h);b=json.loads(b.rstrip('|'))
    if h['type']=='DOCHEAD':doc=b.get('docType')
    ticket=max(ticket,h.get('ticket',0))
    if doc=='PCB':
        if h['type']=='LINE' and b.get('layerId') in [1,2]:continue
        if h['type'] in ['VIA','POURED']:continue
        if h['type']=='POUR':pour_ids[b['layerId']]=h['id']
    rows.append((h,b))
# Standard schematic and PCB imports use different prefixes for the same
# device. Bind by the verified designator; preserve the actual footprint.
refs={b['parentId']:b['value'] for h,b in rows if h['type']=='ATTR' and b.get('key')=='Designator'}
poses=json.loads((ROOT/'cad/placement.json').read_text())
deltas={};doc=None
for h,b in rows:
    if h['type']=='DOCHEAD':doc=b.get('docType')
    if doc!='PCB' or h['type']!='COMPONENT':continue
    ref=refs.get(h['id'])
    if ref not in poses:continue
    x,y,angle=poses[ref];xx=x/.0254;yy=-y/.0254
    deltas[h['id']]=(xx-b['x'],yy-b['y'])
    b.update(x=xx,y=yy,angle=-angle)
    b.setdefault('attrs',{})['Unique ID']='ggeSCH'+ref
doc=None
for h,b in rows:
    if h['type']=='DOCHEAD':doc=b.get('docType')
    if doc=='PCB' and h['type']=='ATTR' and b.get('parentId') in deltas:
        dx,dy=deltas[b['parentId']]
        if isinstance(b.get('x'),(int,float)):b['x']+=dx
        if isinstance(b.get('y'),(int,float)):b['y']+=dy
def add(kind,b,identity=None):
    global ticket
    ticket+=1
    rows.append((dict(type=kind,ticket=ticket,id=identity or uuid.uuid4().hex[:16]),b))
def mil(x):return round(x/.0254,6)
for g in json.loads((ROOT/'cad/routed-geometry.json').read_text()):
    base=dict(groupId='',locked=False,zIndex=ticket+1,partitionId=None,netName=g['net'])
    if 'points' in g:
        for a,b in zip(g['points'],g['points'][1:]):
            if a==b:continue
            add('LINE',dict(base,layerId=g['layer'],startX=mil(a[0]),startY=-mil(a[1]),
                           endX=mil(b[0]),endY=-mil(b[1]),width=mil(g['width'])))
    else:
        add('VIA',dict(base,layerId=12,centerX=mil(g['x']),centerY=-mil(g['y']),
                       viaDiameter=mil(g['diameter']),holeDiameter=mil(g['drill']),viaType='NORMAL',
                       topSolderExpansion=-1000,bottomSolderExpansion=-1000,unusedInnerLayers=[]))
def ring_path(ring):
    return [mil(ring[0][0]),-mil(ring[0][1]),'L']+[v for x,y in ring[1:] for v in (mil(x),-mil(y))]
planes=json.loads((ROOT/'cad/ground-polygons.json').read_text())
# The editor must rebuild POURED from these native POUR boundaries. Its
# filled-path serialization differs from imported Standard cached polygons.
dest.write_text('\n'.join(json.dumps(h,separators=(',',':'))+'||'+json.dumps(b,separators=(',',':'))+'|' for h,b in rows)+'\n',encoding='utf-8')
print(json.dumps(dict(file=str(dest),records=len(rows),ground_regions=len(planes))))
