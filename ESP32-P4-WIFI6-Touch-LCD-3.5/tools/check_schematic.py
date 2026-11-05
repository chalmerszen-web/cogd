"""Compare native Protel2 export with the reviewed PDF vector reconstruction."""
from pathlib import Path
import json,sys,base64
ROOT=Path(__file__).resolve().parents[1]
result=json.loads(Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
assert result['ok'],result
value=result['value'];netlist=value.get('netlist',value.get('protel'))
model=json.loads((ROOT/'hardware/source-netlist.json').read_text(encoding='utf-8-sig'))
expected={c['ref']+'-'+pin:net for c in model['components'] for pin,net in c['pins'].items() if net}
actual={};active=False;name=None
for line in netlist.splitlines():
    line=line.strip()
    if not line:continue
    if line=='(':active=True;name=None
    elif line==')':active=False
    elif active and name is None:name=line
    elif active:
        pin=line.split()[0]
        assert pin not in actual or actual[pin]==name,pin
        actual[pin]=name
mismatch={p:{'expected':n,'actual':actual.get(p)} for p,n in expected.items() if actual.get(p)!=n}
extra={p:n for p,n in actual.items() if p not in expected}
report={'status':'FAIL' if mismatch or extra else 'PASS','source_sha256':model['source_sha256'],
        'source_pins':len(expected),'native_pins':len(actual),'nets':len(set(actual.values())),
        'missing_or_mismatched':mismatch,'unexpected':extra,'pages':value.get('pages'),
        'drc':value.get('drc','NOT RUN'),'source_basis':'Vector reconstruction; native roundtrip is not independent source electrical verification','footprints':'UNASSIGNED; schematic only',
        'electrical_pin_types':'Passive; directional ERC not claimed'}
(ROOT/'evidence/native-connectivity-check.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
(ROOT/'hardware/native-export.net').write_text(netlist.replace('\r',''),encoding='utf8')
if value.get('png'):
    data=base64.b64decode(value['png'].split(',',1)[1]);name=value.get('fileName') or 'native-export.png'
    (ROOT/'hardware'/Path(name).name).write_bytes(data)
print(json.dumps({k:v for k,v in report.items() if k not in ('pages','drc')},ensure_ascii=False))
assert not mismatch and not extra,'Native connectivity differs from source'
