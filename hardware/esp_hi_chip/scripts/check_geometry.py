"""Independent millimetre-space copper clearance and connectivity checks.

Uses source geometry, not the routing grid. Native LCEDA DRC is still required.
"""
from pathlib import Path
import sys,json
import numpy as np
ROOT=Path(__file__).resolve().parents[1];CAD=ROOT/'cad'
sys.path.append(str(ROOT.parents[1]/'.local/pcb-python'))
from shapely.geometry import Point,LineString,box,Polygon
from shapely.affinity import rotate,translate
from shapely.strtree import STRtree
from shapely.ops import unary_union
MIN_CLEARANCE=.125

def load_objects():
    pads=json.loads((CAD/'pads-mm.json').read_text())
    geo=json.loads((CAD/'routed-geometry.json').read_text())
    objs=[]
    for p in pads:
        g=Point(0,0).buffer(p['w']/2,quad_segs=24) if p['ref'].startswith('TP') else box(-p['w']/2,-p['h']/2,p['w']/2,p['h']/2)
        g=translate(rotate(g,p['rotation'],origin=(0,0)),p['x'],p['y'])
        objs.append(dict(name=p['ref']+'.'+p['pin'],net=p['net'] or '@NC:'+p['ref']+'.'+p['pin'],layers=[1],g=g,pad=True))
    for i,a in enumerate(geo):
        if 'points' in a:
            if len(set(map(tuple,a['points'])))<2:continue
            g=LineString(a['points']).buffer(a['width']/2,quad_segs=12)
            objs.append(dict(name='route'+str(i),net=a['net'],layers=[a['layer']],g=g,pad=False))
        else:
            g=Point(a['x'],a['y']).buffer(a['diameter']/2,quad_segs=24)
            objs.append(dict(name='via'+str(i),net=a['net'],layers=[1,2],g=g,pad=False))
    p=json.loads((CAD/'ESP-HI-C3-RevA-PCB-placement.json').read_text())
    for s in p['shape']:
        for t in s.split('#@$'):
            if t.startswith('HOLE~'):
                a=t.split('~');x,y,r=[float(v)*.254 for v in a[1:4]]
                objs.append(dict(name=a[4],net='@HOLE:'+a[4],layers=[1,2],g=Point(x,y).buffer(r,quad_segs=24),pad=False))
    plane_file=CAD/'ground-polygons.json'
    if plane_file.exists():
        for i,p in enumerate(json.loads(plane_file.read_text())):
            objs.append(dict(name='plane'+str(i),net='GND',layers=[p['layer']],g=Polygon(p['exterior'],p['holes']),pad=False))
    return objs

def check(objs):
    parent=list(range(len(objs)))
    def root(i):
        while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
        return i
    bad=[]
    for layer in [1,2]:
        inds=[i for i,a in enumerate(objs) if layer in a['layers']]
        gs=[objs[i]['g'] for i in inds];tree=STRtree(gs)
        for k,i in enumerate(inds):
            a=objs[i]
            for v in tree.query(a['g'].buffer(MIN_CLEARANCE+.0001)):
                j=inds[int(v)]
                if j<=i:continue
                b=objs[j];d=a['g'].distance(b['g'])
                if a['net']==b['net']:
                    if d<.0001:parent[root(i)]=root(j)
                elif d<MIN_CLEARANCE-.001:
                    bad.append(dict(layer=layer,a=a['name'],b=b['name'],net_a=a['net'],net_b=b['net'],clearance_mm=round(d,6)))
    groups={}
    for i,a in enumerate(objs):
        if a['pad'] and not a['net'].startswith('@'):
            groups.setdefault(a['net'],{}).setdefault(root(i),[]).append(a['name'])
    remaining={n:list(g.values()) for n,g in groups.items() if len(g)>1}
    report={'clearance_rule_mm':MIN_CLEARANCE,'numeric_tolerance_mm':.001,'clearance_violations':bad,
            'unconnected_nets':remaining,'status':'PASS' if not bad and not remaining else 'FAIL'}
    (ROOT/'evidence/geometry-check.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':report['status'],'clearance_violations':len(bad),
                      'unconnected_nets':{n:len(g)-1 for n,g in remaining.items()},'first_violations':bad[:8]}))
    return report

if __name__=='__main__':check(load_objects())
