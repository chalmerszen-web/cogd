"""Diagnostic geometry only; does not export an electrical schematic."""
from pathlib import Path
import sys,json,collections,math,re
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parent/'.local/pcb-python'))
import pymupdf
doc=pymupdf.open(ROOT/'hardware/references/original.pdf');page=doc[0]
parts={};ref=None;mode=None
for level,title,_ in doc.get_toc():
    if level==3:mode=title
    elif mode=='Components' and level==4:ref=title;parts[ref]=[]
    elif mode=='Components' and level==5:parts[ref].append(title[len(ref)+1:])
spans=[s for b in page.get_text('dict')['blocks'] if 'lines'in b for l in b['lines'] for s in l['spans']]
hidden={}
for s in spans:
    if s['text'].startswith('PI'):hidden.setdefault(s['text'],s)
rawspans=[s for b in page.get_text('rawdict')['blocks'] if 'lines'in b for l in b['lines'] for s in l['spans']]
segments=[]
for d in page.get_drawings():
    color=tuple(round(v,3) for v in d['color']) if d['color'] else None
    if color is None:continue
    for item in d['items']:
        if item[0]!='l':continue
        a,b=item[1:];length=abs(a-b)
        if not .2<length<16:continue
        if abs(a.x-b.x)>.03 and abs(a.y-b.y)>.03:continue
        segments.append({'a':list(a),'b':list(b),'color':color})
rows=[]
for ref,pins in parts.items():
    for pin in pins:
        tag='PI'+ref+'0'+pin.replace('/','0');s=hidden.get(tag)
        if not s:
            for raw in rawspans:
                chars=raw['chars'];text=''.join(c['c'] for c in chars)
                match=re.search(re.escape(tag)+r'(?=PI|CO|\s|$)',text)
                if match:
                    boxes=[pymupdf.Rect(c['bbox']) for c in chars[match.start():match.end()]]
                    box=boxes[0]
                    for extra in boxes[1:]:box|=extra
                    s={'origin':chars[match.start()]['origin'],'bbox':list(box)};break
        if not s and ref=='U7' and pin=='100':
            # The PDF omitted this invisible tag. Its visible pin number and
            # XTAL_P pin were checked in evidence/u7-pin100.png.
            s={'origin':(527,286.544),'bbox':(527,280.8,529,288.3)}
        if not s:rows.append({'pin':ref+'-'+pin,'missing_tag':tag});continue
        x,y=s['origin'];candidates=[];box=pymupdf.Rect(s['bbox'])
        for i,d in enumerate(segments):
            px=min(d['a'][0],d['b'][0]);py=min(d['a'][1],d['b'][1])
            def outside(pt):return max(box.x0-pt[0],0,pt[0]-box.x1)+max(box.y0-pt[1],0,pt[1]-box.y1)
            error=outside(d['a'])+outside(d['b'])
            center=((d['a'][0]+d['b'][0])/2,(d['a'][1]+d['b'][1])/2)
            cost=error+0.05*(abs(center[0]-(box.x0+box.x1)/2)+abs(center[1]-(box.y0+box.y1)/2))
            if d['color'][2]>.4:cost+=2 # Black/red pin strokes precede blue symbol extensions.
            if error<1.2:candidates.append((round(cost,3),i))
        candidates.sort();rows.append({'pin':ref+'-'+pin,'tag_xy':[x,y],'bbox':list(box),'candidates':candidates[:3]})
picked=[r['candidates'][0][1] for r in rows if r.get('candidates')]
counts=collections.Counter(picked)
report={'parts':len(parts),'pins':len(rows),'pin_segments':len(segments),'missing_tags':sum('missing_tag'in r for r in rows),
        'no_match':[r for r in rows if not r.get('candidates')],
        'duplicate_segments':{str(k):v for k,v in counts.items() if v>1},'rows':rows,'segments':segments}
(ROOT/'evidence/pdf-pin-geometry.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({'parts':len(parts),'pins':len(rows),'missing_tags':report['missing_tags'],
 'no_match_count':len(report['no_match']),'duplicates':report['duplicate_segments'],
 'no_match_names':[r['pin'] for r in report['no_match']]}))
