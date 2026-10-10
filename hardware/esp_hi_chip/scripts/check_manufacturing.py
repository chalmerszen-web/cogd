"""Independent outline, drill and layer checks; complements native DRC."""
import json,math
from pathlib import Path
from check_geometry import load_objects
from shapely.geometry import box,Point
ROOT=Path(__file__).resolve().parents[1]
geo=json.loads((ROOT/'cad/routed-geometry.json').read_text())
objects=load_objects();board=box(0,0,34.3,46.35)
edge=[];npth=[];holes=[]
for a in objects:
    if a['name'].startswith('gge'):continue
    if not board.buffer(-.299).covers(a['g']):
        edge.append(dict(object=a['name'],net=a['net'],clearance_mm=round(board.boundary.distance(a['g']),6)))
for a in objects:
    if not a['net'].startswith('@HOLE'):continue
    for b in objects:
        if b is a or not set(a['layers'])&set(b['layers']):continue
        if a['g'].distance(b['g'])<.199:npth.append(dict(hole=a['name'],object=b['name'],net=b['net'],clearance_mm=round(a['g'].distance(b['g']),6)))
vias=[(i,a) for i,a in enumerate(geo) if 'diameter' in a]
for i,(ai,a) in enumerate(vias):
    for bi,b in vias[i+1:]:
        d=math.hypot(a['x']-b['x'],a['y']-b['y'])-(a['drill']+b['drill'])/2
        if d<.199:holes.append(dict(a=ai,b=bi,a_net=a['net'],b_net=b['net'],clearance_mm=round(d,6)))
annuli=[dict(index=i,annulus_mm=(a['diameter']-a['drill'])/2) for i,a in vias if (a['diameter']-a['drill'])/2<.0749]
pads=[a for a in objects if a['pad']];pad_spacing=[]
for i,a in enumerate(pads):
    for b in pads[i+1:]:
        if a['net']==b['net']:continue
        d=a['g'].distance(b['g'])
        if d<.149:pad_spacing.append(dict(a=a['name'],b=b['name'],clearance_mm=round(d,6)))
report=dict(status='PASS' if not edge+npth+holes+annuli+pad_spacing else 'FAIL',
    outline_mm=[34.3,46.35],copper_to_edge_rule_mm=.3,npth_to_copper_rule_mm=.2,hole_to_hole_rule_mm=.2,
    edge_violations=edge,npth_violations=npth,hole_spacing_violations=holes,annulus_violations=annuli,
    different_net_smd_pad_clearance_rule_mm=.15,pad_spacing_violations=pad_spacing,
    minimum_track_width_mm=min(a['width'] for a in geo if 'points' in a),
    minimum_annulus_mm=min((a['diameter']-a['drill'])/2 for _,a in vias),
    used_copper_layers=sorted({a['layer'] for a in geo if 'points' in a}))
(ROOT/'evidence/manufacturing-geometry.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:len(v) if isinstance(v,list) and k.endswith('violations') else v for k,v in report.items()}))
