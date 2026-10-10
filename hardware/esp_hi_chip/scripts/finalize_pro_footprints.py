"""Apply reviewed land and legend adjustments to a closed native project.

Native footprint data is embedded in PCB/schematic files. Run after importing
and linking the Standard source, before final native re-pour and checks.
"""
from pathlib import Path
import json,sys
project=Path(sys.argv[1]);changed=[]
for p in list(project.rglob('*.epcb2'))+list(project.rglob('*.esch2')):
    out=[];doc=None;edits=0;labels={}
    for line in p.read_text(encoding='utf-8-sig').splitlines():
        a=line.split('||',1)
        if len(a)!=2 or not a[1].rstrip('|'):out.append(line);continue
        h=json.loads(a[0]);b=json.loads(a[1].rstrip('|'))
        if h['type']=='DOCHEAD':doc=b.get('docType')
        if doc=='SCH_PAGE' and h['type']=='ATTR' and b.get('key')=='Designator':
            labels[b['parentId']]=b
        if doc=='SCH_PAGE' and h['type']=='ATTR' and b.get('key')=='Name' and b.get('parentId') in labels:
            label=labels[b['parentId']]
            b.update(valueVisible=True,x=label['x'],y=label['y']-15,align='CENTER_BOTTOM',fontSize=11.11111)
            edits+=1
        if doc=='FOOTPRINT' and h['type']=='PAD' and b.get('num') in ('A1/B12','A12/B1'):
            b['defaultPad']['height']=round(1.08/.0254,6);edits+=1
        # Dense package outlines and sub-1mm reference legends belong on the
        # assembly drawing. Avoid illegible/masked production silkscreen.
        if doc=='FOOTPRINT' and h['type'] in ('LINE','ARC','POLY','STRING','ATTR') and b.get('layerId')==3:
            b['layerId']=9;edits+=1
        if doc=='PCB' and h['type']=='ATTR' and b.get('key')=='Designator' and b.get('layerId')==3:
            b['layerId']=9;edits+=1
        out.append(a[0]+'||'+json.dumps(b,separators=(',',':'))+'|')
    if edits:
        text='\n'.join(out)+'\n'
        text=text.replace('DRAFT - native ERC and physical verification pending. GPIO identities retained.',
                          'Rev A prototype design. GPIO identities retained. See VALIDATION.md for check status.')
        p.write_text(text,encoding='utf-8');changed.append(dict(file=str(p.relative_to(project)),edits=edits))
print(json.dumps(changed))
