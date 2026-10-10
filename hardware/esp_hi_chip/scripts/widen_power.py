"""Widen power/audio segments where exact clearance allows, then re-pour.

This is a geometric operation, not a current/temperature certification.
"""
import json,math
from pathlib import Path
from check_geometry import load_objects
from shapely.geometry import LineString,box
from shapely.strtree import STRtree
ROOT=Path(__file__).resolve().parents[1];path=ROOT/'cad/routed-geometry.json'
geo=json.loads(path.read_text());objects=[o for o in load_objects() if not o['name'].startswith('plane')]
tree=STRtree([a['g'] for a in objects]);board=box(.30,.30,34.,46.05);changes=[]
limits={'VBUS':.8,'+3V4':.6,'SW':.6,'RF_3V4':.45,'+3V0_LCD':.3,'LCD_LEDA':.3,'SPK_P':.4,'SPK_N':.4}
for i,a in enumerate(geo):
    if 'points' not in a or a['net'] not in limits:continue
    line=LineString(a['points']);target=limits[a['net']]
    if not board.covers(line):continue
    target=min(target,2*line.distance(board.boundary))
    for k in tree.query(line.buffer(target/2+.4)):
        b=objects[int(k)]
        if b['net']==a['net'] or a['layer'] not in b['layers']:continue
        clearance=.3 if b['net'].startswith('@HOLE') else .126
        target=min(target,2*(line.distance(b['g'])-clearance))
    target=math.floor(target*100)/100
    if target<=a['width']+.009:continue
    changes.append(dict(index=i,net=a['net'],old_mm=a['width'],new_mm=target))
    a['width']=target
    next(b for b in objects if b['name']=='route'+str(i))['g']=line.buffer(target/2,quad_segs=12)
path.write_text(json.dumps(geo,indent=2)+'\n')
(ROOT/'evidence/power-width-changes.json').write_text(json.dumps(changes,indent=2)+'\n')
print('Power segments widened:',len(changes))
