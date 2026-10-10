"""Serialize the current measured geometry into LCEDA Standard native JSON."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1];CAD=ROOT/'cad';UNIT=.254
fmt=lambda v:f'{v/UNIT:.6f}'
base=json.loads((CAD/'ESP-HI-C3-RevA-PCB-placement.json').read_text())
geo=json.loads((CAD/'routed-geometry.json').read_text())
for i,g in enumerate(geo):
    if 'points' in g:
        s=f'TRACK~{fmt(g["width"])}~{g["layer"]}~{g["net"]}~'+' '.join(fmt(v) for pt in g['points'] for v in pt)+f'~ggeFIN{i}~0'
    else:s=f'VIA~{fmt(g["x"])}~{fmt(g["y"])}~{fmt(g["diameter"])}~{g["net"]}~{fmt(g["drill"]/2)}~ggeFIN{i}~0'
    base['shape'].append(s)
def path(ring):return 'M '+' L '.join(fmt(x)+' '+fmt(y) for x,y in ring[:-1])+' Z'
plane_path=CAD/'ground-polygons.json'
if plane_path.exists():
    planes=json.loads(plane_path.read_text())
    for layer in [1,2]:
        fill=[]
        for p in planes:
            if p['layer']!=layer:continue
            fill.append([path(p['exterior'])+' '+' '.join(path(h) for h in p['holes'])])
        outline=path([[.3,.3],[34.,.3],[34.,46.05],[.3,46.05],[.3,.3]])
        fields=['COPPERAREA',fmt(.15),str(layer),'GND',outline,fmt(.16),'solid',f'ggeGND{layer}',
                'direct','none',json.dumps(fill,separators=(',',':')),'0',f'GND-{layer}','1',fmt(.2),fmt(.25),fmt(.3),'yes','0']
        base['shape'].append('~'.join(fields))
base['DRCRULE']['Default'].update(trackWidth=.125/UNIT,clearance=.125/UNIT,viaHoleDiameter=.3/UNIT,viaHoleD=.15/UNIT)
base['head']['c_para']['name']='ESP-HI-C3-RevA design review'
(CAD/'ESP-HI-C3-RevA-PCB-routed.json').write_text(json.dumps(base,indent=2)+'\n')
print('Native PCB written:',len(geo),'route/via objects')
