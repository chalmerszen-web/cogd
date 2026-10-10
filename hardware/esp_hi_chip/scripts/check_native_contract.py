"""Recheck exported Protel2 netlists and native-vs-canonical copper/placement."""
from collections import Counter
from pathlib import Path
import json, re

ROOT=Path(__file__).resolve().parents[1]
model=json.loads((ROOT/'design-intent.json').read_text())
expected={f"{c['ref']}-{p}":n for c in model['components'] for p,n in c['pins'].items() if n}
reports={}
for kind in ('schematic','pcb'):
    data=(ROOT/f'exports/ESP-HI-C3-RevA-{kind}.net').read_text(encoding='utf-8-sig')
    actual={};active=False;name=None
    for line in data.splitlines():
        line=line.strip()
        if not line:continue
        if line=='(': active=True;name=None
        elif line==')':active=False
        elif active and name is None:name=line
        elif active and line:actual[line.split()[0]]=name
    mismatch={k:dict(expected=v,actual=actual.get(k)) for k,v in expected.items() if actual.get(k)!=v}
    extra={k:v for k,v in actual.items() if k not in expected}
    report=dict(status='FAIL' if mismatch or extra else 'PASS',intended_connected_pins=len(expected),
                exported_connected_pins=len(actual),missing_or_mismatched=mismatch,unexpected=extra)
    (ROOT/f'evidence/{kind}-netlist-contract.json').write_text(json.dumps(report,indent=2)+'\n')
    reports[kind]=report['status']

pcb=ROOT/'native/ESP-HI-C3-RevA/pcb/ESP-HI-C3-RevA-PCB-routed.epcb2'
rows=[];doc=None
for line in pcb.read_text(encoding='utf-8-sig').splitlines():
    if '||' not in line:continue
    h,b=line.split('||',1);h=json.loads(h)
    if not b.rstrip('|'):continue
    b=json.loads(b.rstrip('|'))
    if h['type']=='DOCHEAD':doc=b.get('docType')
    if doc=='PCB':rows.append((h,b))
native_lines=Counter();native_vias=Counter()
def seg(net,layer,width,p,q):
    return (net,layer,round(width,4),tuple(sorted(tuple(round(v,4) for v in x) for x in (p,q))))
for h,b in rows:
    if h['type']=='LINE' and b.get('layerId') in (1,2):
        native_lines[seg(b['netName'],b['layerId'],b['width']*.0254,
                         (b['startX']*.0254,-b['startY']*.0254),(b['endX']*.0254,-b['endY']*.0254))]+=1
    elif h['type']=='VIA':
        native_vias[(b['netName'],round(b['centerX']*.0254,4),round(-b['centerY']*.0254,4),
                     round(b['viaDiameter']*.0254,4),round(b['holeDiameter']*.0254,4))]+=1
canonical=json.loads((ROOT/'cad/routed-geometry.json').read_text())
lines=Counter(seg(a['net'],a['layer'],a['width'],p,q) for a in canonical if 'points' in a
              for p,q in zip(a['points'],a['points'][1:]) if p!=q)
vias=[(a['net'],a['x'],a['y'],a['diameter'],a['drill']) for a in canonical if 'drill' in a]
unmatched=list(native_vias.elements());via_errors=[]
for a in vias:
    match=next((b for b in unmatched if a[0]==b[0] and all(abs(x-y)<=.0001 for x,y in zip(a[1:],b[1:]))),None)
    if match is None:via_errors.append(a)
    else:unmatched.remove(match)
vias_match=not unmatched and not via_errors
refs={b['parentId']:b['value'] for h,b in rows if h['type']=='ATTR' and b.get('key')=='Designator'}
poses=json.loads((ROOT/'cad/placement.json').read_text());placement_errors=[]
components=[(h,b) for h,b in rows if h['type']=='COMPONENT'];layers=Counter()
for h,b in components:
    ref=refs.get(h['id']);layers[b.get('layerId')]+=1
    if ref not in poses:placement_errors.append({'unknown_ref':ref});continue
    x,y,angle=poses[ref]
    if abs(x-b['x']*.0254)>.001 or abs(y+b['y']*.0254)>.001 or min((-angle-b['angle'])%360,(angle+b['angle'])%360)>.001:
        placement_errors.append({'ref':ref,'intended':poses[ref],'native':[b['x']*.0254,-b['y']*.0254,b['angle']]})
report=dict(status='PASS' if native_lines==lines and vias_match and not placement_errors else 'FAIL',
            native_segments=sum(native_lines.values()),canonical_segments=sum(lines.values()),
            tracks_match=native_lines==lines,vias_match=vias_match,native_vias=sum(native_vias.values()),
            footprint_count=len(components),footprints_by_layer=dict(layers),placement_errors=placement_errors,
            coordinate_comparison_resolution_mm=.0001)
(ROOT/'evidence/native-contract-check.json').write_text(json.dumps(report,indent=2)+'\n')
reports['native_geometry']=report
print(json.dumps(reports,indent=2))
if report['status']!='PASS' or any(v!='PASS' for k,v in reports.items() if k!='native_geometry'):
    raise SystemExit(1)
