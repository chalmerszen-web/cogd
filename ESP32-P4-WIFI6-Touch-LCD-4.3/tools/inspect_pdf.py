from pathlib import Path
import json
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parent/'.local/pcb-python'))
import pymupdf
doc=pymupdf.open(ROOT/'hardware/references/original.pdf')
page=doc[0]
spans=[s for b in page.get_text('dict')['blocks'] if 'lines' in b for l in b['lines'] for s in l['spans'] if s['alpha']]
for ref in ['U8','C40','R1','J3','U14']:
    a=next(s for s in spans if s['text']==ref)
    x,y=a['origin']
    closest=sorted(spans,key=lambda s:abs(s['origin'][0]-x)+abs(s['origin'][1]-y))
    print(ref,[(s['text'],s['origin'],s['size'],s['color']) for s in closest[:6]])
path=ROOT.parent/'hardware/esp_hi_chip/native/ESP-HI-C3-RevA/sch/ESP-HI-C3-Chip-RevA/01 MCU, crystal and reset.esch2'
rec=[(json.loads(l.split('||')[0]),json.loads(l.split('||')[1][:-1])) for l in path.read_text().splitlines()]
idx=next(i for i,(a,b) in enumerate(rec) if a['type']=='DOCHEAD' and b['docType']=='SCH_PAGE')
print('sheet',rec[idx:idx+12])
print('wire/label',[(a,b) for a,b in rec[idx:] if a['type'] in ['WIRE','NETLABEL']][:3])
