"""Recover Altium PDF bookmark connectivity into editable LCEDA Pro objects.

Pin numbers and nets come from the source bookmark tree, never line guessing.
Values come from visible PDF text. Footprints are deliberately unassigned.
"""
from pathlib import Path
import hashlib,json,re,sys,uuid,math,collections,time
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parent/'.local/pcb-python'))
import pymupdf
SOURCE=ROOT/'hardware/references/original.pdf'
NAME='ESP32-P4-WIFI6-Touch-LCD-4.3-Reference'
OUT=ROOT/'hardware/native'/NAME
VERSION=str(int(time.time()*1000))
def uid(s):return uuid.uuid5(uuid.NAMESPACE_URL,NAME+'/'+s).hex[:16]
def write_json(p,o):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(o,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
doc=pymupdf.open(SOURCE)
toc=doc.get_toc()
parts={};nets={};mode=None;ref=None;net=None;sub=None
for level,title,page in toc:
    if level==1 and page==2:break
    if level==3:mode=title
    elif mode=='Components' and level==4:
        ref=title;parts[ref]={'ref':ref,'pins':{},'value':'UNSPECIFIED (PDF)','xy':None}
    elif mode=='Components' and level==5:
        assert title.startswith(ref+'-'),title
        parts[ref]['pins'][title[len(ref)+1:]]=None
    elif mode=='Nets' and level==4:
        net=title;nets[net]=[]
    elif mode=='Nets' and level==5:sub=title
    elif mode=='Nets' and level==6 and sub=='Pins':
        r,pin=title.rsplit('-',1)
        assert r in parts and pin in parts[r]['pins'],title
        assert parts[r]['pins'][pin] is None,(title,net)
        if net.lower() not in ('no net','no_net'):
            parts[r]['pins'][pin]=net;nets[net].append(title)
spans=[s for b in doc[0].get_text('dict')['blocks'] if 'lines' in b for l in b['lines'] for s in l['spans'] if s['alpha'] and s['text'].strip()]
blue=[s for s in spans if s['color'] in (128,255) or s['text'].strip() in parts]
for i,s in enumerate(blue):
    ref=s['text'].strip()
    if ref not in parts:
        matches=[r for r in parts if re.search(r'(?<![A-Za-z0-9])'+re.escape(r)+r'(?![A-Za-z0-9])',ref)]
        if len(matches)!=1:continue
        ref=matches[0]
    parts[ref]['xy']=list(s['origin'])
    if i+1<len(blue):
        following=blue[i+1]['text'].strip()
        if following not in parts:parts[ref]['value']=following
# These rotated labels were visually checked in the original PDF (evidence/*detail.png).
for ref,value in {'C85':'1uF','C99':'1uF','C114':'1uF','C134':'22pF','C154':'1uF'}.items():
    parts[ref]['value']=value
def block(p):
    if not p['xy']:return '12 Other'
    x,y=p['xy']
    if y<117:
        return '01 USB' if x<210 else '02 Power' if x<347 else '03 MicroSD' if x<485 else '04 ESP32-C6 WiFi'
    if y<235:return '02 Power' if x<282 else '05 USB UART' if x<380 else '06 ESP32-P4'
    if y<322:return '07 Battery' if x<221 else '08 Camera and buttons' if x<380 else '06 ESP32-P4'
    if y<451:return '09 Display and backlight' if x<380 else '06 ESP32-P4'
    return '10 Audio' if x<629 else '11 Expansion'
for p in parts.values():p['block']=block(p)
model={'source_url':'https://www.waveshare.net/w/upload/b/b8/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf',
       'source_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),'components':list(parts.values()),'nets':{k:v for k,v in nets.items() if v},
       'notes':['Source PCB Rev1.2; physical device flash is 16MiB versus source 32MiB.','Pin types are passive/undefined; footprints are unassigned.','Layout is reorganized by source region; connectivity is recovered from PDF bookmarks.']}
write_json(ROOT/'hardware/source-netlist.json',model)

class Document:
    def __init__(self,kind,name,identifier):
        self.rows=[];self.n=0
        self.rows.append(json.dumps({'type':'DOCHEAD'})+'||'+json.dumps({'docType':kind,'uuid':identifier,'client':uid('client'),'editVersion':'4.1.71','version':VERSION,'updateTime':int(VERSION),'user':{'uuid':'CONFIG'}})+'|')
        self.add('META','META',{'title':name,'source':'','tags':[],**({'docType':2} if kind=='SYMBOL' else {})})
    def add(self,t,ident=None,data=None,**kw):
        self.n+=1;ident=ident or 'e'+str(self.n)
        payload=data if data is not None else kw
        self.rows.append(json.dumps({'type':t,'ticket':self.n,'id':ident},separators=(',',':'))+'||'+json.dumps(payload,ensure_ascii=False,separators=(',',':'))+'|')
        return ident
    def graphic(self,t,part='',**kw):return self.add(t,partId=part,groupId='',locked=False,zIndex=self.n,yAxisDirection='up',**kw)
    def text(self,value,x,y,size=10,part='',parent=None,key=None,visible=True,align='LEFT_BOTTOM'):
        kw=dict(x=x,y=y,rotation=0,color=None,fillColor=None,fontFamily=None,fontSize=size,strikeout=False,underline=False,italic=False,fontWeight=False,align=align)
        if parent:return self.graphic('ATTR',part,parentId=parent,key=key,value=value,keyVisible=False,valueVisible=visible,**kw)
        return self.graphic('TEXT',part,value=value,**kw)
def dimensions(p):return 120 if len(p['pins'])<=2 else max(180,(math.ceil(len(p['pins'])/2)+1)*20+80)
def library(p):
    ref=p['ref'];n=len(p['pins']);part=ref+'.1';h=max(40,(math.ceil(n/2)+1)*20);w=130 if n>2 else 40
    sym=Document('SYMBOL',ref,uid('symbol/'+ref));sym.add('CANVAS','CANVAS',{'originX':0,'originY':0,'yAxisDirection':'up'})
    sym.add('PART',part,{'title':part})
    # Deliberately use generic editable symbols with the exact source pin numbers.
    sym.graphic('RECT',part,dotX1=-w/2,dotY1=h/2,dotX2=w/2,dotY2=-h/2,radiusX=0,radiusY=0,rotation=0,strokeColor=None,strokeWidth=1,strokeStyle='SOLID',fillColor=None,fillStyle='NONE')
    loc=[]
    for i,pin in enumerate(p['pins']):
        left=i<math.ceil(n/2);row=i if left else i-math.ceil(n/2)
        x=(-1 if left else 1)*(w/2+20);y=h/2-20-row*20
        if n==2:y=0
        pid=sym.graphic('PIN',part,display=True,x=x,y=y,length=20,rotation=0 if left else 180,color=None,pinShape='NONE')
        sym.text(pin,x+(8 if left else -8),y+4,8,part,pid,'Pin Number',align='CENTER_BOTTOM')
        sym.text(pin,x+(23 if left else -23),y-3,8,part,pid,'Pin Name',n>2,'LEFT_BOTTOM' if left else 'RIGHT_BOTTOM')
        sym.text('Passive',None,None,8,part,pid,'Pin Type',False)
        loc.append((pin,x,y,left))
    device=Document('DEVICE',p['value'],uid('device/'+ref))
    device.add('META','META',{'title':p['value'],'tags':[],'source':'','images':[],
        'attributes':{'Symbol':uid('symbol/'+ref),'Footprint':None,'Designator':re.sub(r'\d+','',ref)+'?',
        'Name':p['value'],'Description':'Converted from Waveshare PDF; footprint not assigned','Add into BOM':'yes','Convert to PCB':'yes'}})
    return sym.rows+device.rows,loc,h

schid=uid('schematic');boardid=uid('board');sheets={};sheet_count=0
for group in sorted(set(p['block'] for p in parts.values())):
    members=[p for p in parts.values() if p['block']==group]
    members.sort(key=lambda p:(-len(p['pins']),p['xy'][1] if p['xy'] else 0,p['ref']))
    batches=[];batch=[];heights=[100,100,100]
    for p in members:
        col=min(range(3),key=lambda c:heights[c]);height=dimensions(p)
        if heights[col]+height>1600 and batch:
            batches.append(batch);batch=[];heights=[100,100,100];col=0
        batch.append((p,350+750*col,-heights[col]-height/2));heights[col]+=height
    if batch:batches.append(batch)
    for b,batch in enumerate(batches,1):
        title=group+(f' - {b}' if len(batches)>1 else '');sheet_count+=1;sid=uid('sheet/'+title)
        page=Document('SCH_PAGE',title,sid);page.add('META','META',{'title':title,'schematic':schid,'source':'','zIndex':sheet_count})
        page.add('CANVAS','CANVAS',{'originX':0,'originY':0,'yAxisDirection':'up'})
        page.text(NAME+' / '+title,40,-30,18)
        page.text('PDF connectivity reconstruction. Values editable. Footprints unassigned. Source PCB Rev1.2.',40,-55,10)
        libraries=[]
        for p,cx,cy in batch:
            ref=p['ref'];lib,loc,h=library(p);libraries+=lib
            comp=page.graphic('COMPONENT',ref+'.1',x=cx,y=cy,rotation=0,isMirror=False,attrs={'Unique ID':'P4_'+ref})
            for key,val in [('Device',uid('device/'+ref)),('Symbol',uid('symbol/'+ref)),('Footprint',None),('Designator',ref),('Name',p['value']),('Add into BOM','yes'),('Convert to PCB','yes')]:
                page.text(val,cx,cy+h/2+(32 if key=='Designator' else 16),11 if key=='Designator' else 9,'',comp,key,key in ('Designator','Name'),'CENTER_BOTTOM')
            for pin,x,y,left in loc:
                net=p['pins'][pin]
                if net is None:continue
                # Generated symbols define these exact pins; native exports independently verify them.
                x+=cx;y+=cy;end=x+(-30 if left else 30)
                wire=page.graphic('WIRE')
                page.add('LINE',lineGroup=wire,startX=x,startY=y,endX=end,endY=y,strokeColor=None,strokeWidth=1,strokeStyle='SOLID',fillColor=None,fillStyle='NONE',yAxisDirection='up')
                page.text(net,end,y+4,9,'',wire,'NET',True,'RIGHT_BOTTOM' if left else 'LEFT_BOTTOM')
        target=OUT/'sch'/'Reference'/f'{title}.esch2';target.parent.mkdir(parents=True,exist_ok=True)
        target.write_text('\n'.join(libraries+page.rows)+'\n',encoding='utf8')
        sheets[sid]={'uuid':sid,'title':title,'schematic_uuid':schid,'zIndex':sheet_count,'source':'','version':VERSION}
index={'name':NAME,'format':'folder','content':'','ticket':1,'g_ticket':1,'boards':[],
       'config':{'defaultSheet':next(iter(sheets)),'settings':{}},'profile':{'boards':{boardid:{'uuid':boardid,'title':'Reference','zIndex':0}},
       'schematics':{schid:{'uuid':schid,'name':'Reference','board':boardid,'source':'','version':VERSION}},'sheets':sheets,'pcbs':{}}}
write_json(OUT/(NAME+'.eprj3'),index)
report={'components':len(parts),'pins':sum(len(p['pins']) for p in parts.values()),'connected_pins':sum(len(v) for v in nets.values()),
        'nets':len(model['nets']),'sheets':sheet_count,'unspecified_values':[p['ref'] for p in parts.values() if p['value'].startswith('UNSPECIFIED')],
        'native_import':'NOT RUN','native_netlist_comparison':'NOT RUN','footprints':'UNASSIGNED'}
write_json(ROOT/'evidence/schematic-conversion.json',report)
print(json.dumps(report,ensure_ascii=False))
