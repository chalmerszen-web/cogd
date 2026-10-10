"""Construct conservative ground pours and tented through-via stitching.

Outputs explicit filled geometry as well as native editable copper-area outlines.
Native LCEDA rebuild/DRC and independent geometry checks remain separate gates.
"""
from pathlib import Path
import json,sys
import numpy as np
from check_geometry import load_objects
from shapely.geometry import box,Point,Polygon
from shapely.ops import unary_union,nearest_points
ROOT=Path(__file__).resolve().parents[1];CAD=ROOT/'cad';UNIT=.254
geo_path=CAD/'routed-geometry.json'
geo=json.loads(geo_path.read_text())
geo=[a for a in geo if a.get('purpose') not in ['ground_stitch','thermal_ground']]
geo_path.write_text(json.dumps(geo,indent=2)+'\n')
objects=[o for o in load_objects() if not o['name'].startswith('plane')]
boundary=box(.3,.3,34.,46.05)
non_ground=[o for o in objects if o['net']!='GND']
npth=unary_union([o['g'] for o in non_ground if o['net'].startswith('@HOLE')])
obstacles={l:unary_union([o['g'] for o in non_ground if l in o['layers'] and not o['net'].startswith('@HOLE')]) for l in [1,2]}
ground_pads=[o for o in objects if o['pad'] and o['net']=='GND']
all_pads=[o['g'] for o in objects if o['pad'] and o['name']!='U1.33']
allowed=boundary.buffer(-.225).difference(unary_union([o['g'] for o in non_ground]).buffer(.38))
allowed=allowed.difference(npth.buffer(.535))
allowed=allowed.difference(unary_union(all_pads).buffer(.30))
planes={}
for l in [1,2]:
    g=boundary.difference(obstacles[l].buffer(.13,quad_segs=12)).difference(npth.buffer(.305,quad_segs=12))
    # Eliminate copper slivers/necks below approximately 0.12 mm.
    g=g.buffer(-.06,quad_segs=8).buffer(.06,quad_segs=8)
    planes[l]=[p for p in (g.geoms if hasattr(g,'geoms') else [g]) if p.area>.03]

vias=[]
def add_via(p,purpose='ground_stitch'):
    if not allowed.covers(p):return False
    if any(Point(v['x'],v['y']).distance(p)<.55 for v in vias):return False
    vias.append(dict(net='GND',x=round(p.x,5),y=round(p.y,5),diameter=.45,drill=.2,purpose=purpose))
    return True

# Spread the thermal vias across the exposed pad; no SMD pin via-in-pad is used.
for y in [17.1,18.,18.9]:
    for x in [16.3,17.2,18.1]:add_via(Point(x,y),'thermal_ground')

# First connect each top-side ground island that contains a physical land.
for p in sorted(planes[1],key=lambda q:q.area):
    if not any(p.intersects(o['g']) for o in ground_pads):continue
    if any(p.covers(Point(v['x'],v['y'])) for v in vias):continue
    space=p.intersection(allowed)
    if not space.is_empty:
        parts=list(space.geoms) if hasattr(space,'geoms') else [space]
        q=max(parts,key=lambda z:z.area)
        add_via(q.representative_point())

# Additional return-path stitching, including the RF/crystal region.
for y in np.arange(1.,37.,2.5):
    for x in np.arange(1.,34.,2.5):add_via(Point(float(x),float(y)))
for o in ground_pads:
    p=o['g'].centroid
    nearby=allowed.intersection(p.buffer(1.1))
    if nearby.is_empty:continue
    q=nearest_points(p,nearby.buffer(-.003))[1] if not nearby.buffer(-.003).is_empty else nearby.representative_point()
    if min((Point(v['x'],v['y']).distance(q) for v in vias),default=100)>.9:add_via(q)

geo+=vias;geo_path.write_text(json.dumps(geo,indent=2)+'\n')
out=[]
for layer,polys in planes.items():
    for p in polys:
        # Omit floating islands that touch neither a ground via nor a ground pad.
        contacts=[Point(v['x'],v['y']).buffer(.225) for v in vias]
        if layer==1:contacts += [o['g'] for o in ground_pads]
        if not any(p.intersects(c) for c in contacts):continue
        p=p.simplify(.002,preserve_topology=True)
        out.append(dict(layer=layer,exterior=list(p.exterior.coords),holes=[list(h.coords) for h in p.interiors]))
(CAD/'ground-polygons.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps({'ground_vias':len(vias),'thermal_vias':sum(v['purpose']=='thermal_ground' for v in vias),
                  'ground_islands_by_layer':{l:sum(p['layer']==l for p in out) for l in [1,2]}}))
