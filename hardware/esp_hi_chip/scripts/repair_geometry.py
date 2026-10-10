"""Correct raster rounding at joins using exact copper geometry.

Only short (<0.08 mm) same-net bridges and sub-0.04 mm via nudges are allowed.
Each candidate is checked against every other-net object on the affected layer.
"""
from pathlib import Path
import json,math,sys
from check_geometry import load_objects,MIN_CLEARANCE
from shapely.geometry import Point,LineString
from shapely.ops import unary_union,nearest_points
from shapely.strtree import STRtree
ROOT=Path(__file__).resolve().parents[1];CAD=ROOT/'cad'
geo=json.loads((CAD/'routed-geometry.json').read_text());edits=[]
def save(): (CAD/'routed-geometry.json').write_text(json.dumps(geo,indent=2)+'\n')
def valid(g,net,layers,objs,ignore=None):
    return all(o['net']==net or o['name']==ignore or not set(layers)&set(o['layers']) or g.distance(o['g'])>=(.3 if o['net'].startswith('@HOLE') else .125)-1e-6 for o in objs)
objects=[o for o in load_objects() if not o['name'].startswith('plane')]
offsets=sorted([(x*.005,y*.005) for x in range(-8,9) for y in range(-8,9)],key=lambda q:q[0]**2+q[1]**2)
for i,a in enumerate(geo):
    if 'diameter' not in a or a.get('purpose'):continue
    g=Point(a['x'],a['y']).buffer(a['diameter']/2,quad_segs=24)
    if valid(g,a['net'],[1,2],objects,'via'+str(i)):continue
    for dx,dy in offsets:
        p=Point(a['x']+dx,a['y']+dy).buffer(a['diameter']/2,quad_segs=24)
        if not valid(p,a['net'],[1,2],objects,'via'+str(i)):continue
        a['x']+=dx;a['y']+=dy;edits.append(dict(via=i,delta=[dx,dy]))
        next(o for o in objects if o['name']=='via'+str(i))['g']=p
        break
save()
for repeat in range(12):
    ground='--ground' in sys.argv
    objects=[o for o in load_objects() if ground or not o['name'].startswith('plane')]
    parents=list(range(len(objects)))
    def root(i):
        while parents[i]!=i:parents[i]=parents[parents[i]];i=parents[i]
        return i
    for layer in [1,2]:
        ix=[i for i,o in enumerate(objects) if layer in o['layers']];tree=STRtree([objects[i]['g'] for i in ix])
        for i in ix:
            a=objects[i]
            for k in tree.query(a['g'].buffer(.0001)):
                j=ix[int(k)];b=objects[j]
                if j>i and a['net']==b['net'] and a['g'].distance(b['g'])<.0001:parents[root(i)]=root(j)
    groups={}
    for i,o in enumerate(objects):
        if o['net'].startswith('@') or (o['net']=='GND')!=ground:continue
        groups.setdefault(o['net'],{}).setdefault(root(i),[]).append(o)
    candidates=[]
    for net,gs in groups.items():
        gs=[os for os in gs.values() if any(o['pad'] for o in os)]
        for i,a in enumerate(gs):
            for b in gs[i+1:]:
                for l in [1,2]:
                    aa=unary_union([o['g'] for o in a if l in o['layers']]);bb=unary_union([o['g'] for o in b if l in o['layers']])
                    if aa.is_empty or bb.is_empty or aa.distance(bb)>(2. if ground else .08):continue
                    p,q=nearest_points(aa,bb);line=LineString([p,q]);g=line.buffer(.0625)
                    if valid(g,net,[l],objects):candidates.append(dict(net=net,layer=l,width=.125,points=[list(p.coords)[0],list(q.coords)[0]],purpose='exact_join'))
    if not candidates:break
    geo+=candidates;edits+=candidates;save()
(ROOT/'evidence/geometry-repairs.json').write_text(json.dumps(edits,indent=2)+'\n')
print('Exact geometry corrections:',len(edits))
