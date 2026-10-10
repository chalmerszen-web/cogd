"""Complete remaining signal connections on a conservative two-layer grid.

The result is a review draft; native LCEDA DRC and the independent geometry
check remain mandatory. Existing routing is retained. Ground is poured later.
"""
from pathlib import Path
import json, math, heapq, sys, os, time
import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import label, distance_transform_edt
from numba import njit

ROOT=Path(__file__).resolve().parents[1]; CAD=ROOT/'cad'
STEP=.025 if '--fine-grid' in sys.argv else .05
W=round(34.30/STEP)+1;H=round(46.35/STEP)+1;SHAPE=(2,H,W);UNIT=.254
PADS=json.loads((CAD/'pads-mm.json').read_text())
GEO=json.loads((CAD/'routed-geometry.json').read_text())
if '--fresh' in sys.argv:
    fixed=json.loads((CAD/'critical-routing.json').read_text())
    GEO=fixed['traces']+fixed['vias']
NETS=sorted({p['net'] for p in PADS if p['net']}); IDS={n:i+1 for i,n in enumerate(NETS)}

def pix(x,y):return round(x/STEP),round(y/STEP)
def draw_geo(im,g,value):
    d=ImageDraw.Draw(im)
    if 'points' in g:
        pts=[pix(*p) for p in g['points']];r=g['width']/2/STEP
        d.line(pts,fill=value,width=max(1,math.ceil(2*r)))
        for x,y in pts:d.ellipse((x-r,y-r,x+r,y+r),fill=value)
    elif 'diameter' in g:
        x,y=pix(g['x'],g['y']);r=g['diameter']/2/STEP
        d.ellipse((x-r,y-r,x+r,y+r),fill=value)
    else:
        x,y=pix(g['x'],g['y']);w,h=g['w']/STEP,g['h']/STEP
        if round(g['rotation'])%180:w,h=h,w
        box=(x-w/2,y-h/2,x+w/2,y+h/2)
        if g['ref'].startswith('TP'):d.ellipse(box,fill=value)
        else:d.rectangle(box,fill=value)

ims=[Image.new('I',(W,H),0),Image.new('I',(W,H),0)]
padim=Image.new('I',(W,H),0)
if '--ground' in sys.argv:
    for p in json.loads((CAD/'ground-polygons.json').read_text()):
        mask=Image.new('1',(W,H),0);d=ImageDraw.Draw(mask)
        d.polygon([pix(*q) for q in p['exterior']],fill=1)
        for h in p['holes']:d.polygon([pix(*q) for q in h],fill=0)
        ims[p['layer']-1].paste(IDS['GND'],mask=mask)
for p in PADS:
    draw_geo(ims[0],p,IDS.get(p['net'],999))
    draw_geo(padim,p,1)
for g in GEO:
    for layer in ([g['layer']-1] if 'points' in g else [0,1]):draw_geo(ims[layer],g,IDS[g['net']])
# NPTH obstacles are real on both copper layers.
pcb=json.loads((CAD/'ESP-HI-C3-RevA-PCB-placement.json').read_text())
for shape in pcb['shape']:
    for t in shape.split('#@$'):
        if t.startswith('HOLE~'):
            a=t.split('~');x,y,r=[float(v)*UNIT for v in a[1:4]]
            for im in ims:draw_geo(im,dict(x=x,y=y,diameter=2*(r+.175)),999)
board=np.stack([np.array(im,dtype=np.int32) for im in ims])
pad_distance=distance_transform_edt(np.array(padim)==0)
via_pad_ok=pad_distance>=(.155 if '--ground' in sys.argv and '--micro-vias' in sys.argv else .225 if '--micro-vias' in sys.argv else .275)/STEP

@njit
def search(free,via_ok,source,target,heuristic,penalty):
    _,h,w=free.shape;N=2*h*w;wh=h*w
    dist=np.full(N,1e30,np.float64);prev=np.full(N,-1,np.int32)
    done=np.zeros(N,np.uint8);q=[(0.0,0)];heapq.heappop(q)
    for z in range(2):
        for y in range(h):
            for x in range(w):
                if source[z,y,x]:
                    i=z*wh+y*w+x;dist[i]=0;heapq.heappush(q,(heuristic[y,x],i))
    steps=0;end=-1
    while q:
        _,i=heapq.heappop(q)
        if done[i]:continue
        done[i]=1;steps+=1
        z=i//wh;t=i%wh;y=t//w;x=t%w
        if target[z,y,x]:end=i;break
        for dy in range(-1,2):
            for dx in range(-1,2):
                if dx==0 and dy==0:continue
                xx=x+dx;yy=y+dy
                if xx<0 or xx>=w or yy<0 or yy>=h or not free[z,yy,xx]:continue
                if dx and dy and (not free[z,y,xx] or not free[z,yy,x]):continue
                j=z*wh+yy*w+xx;v=dist[i]+(1.41421356237 if dx and dy else 1.)+penalty[z,yy,xx]
                if v<dist[j]:dist[j]=v;prev[j]=i;heapq.heappush(q,(v+heuristic[yy,xx],j))
        if via_ok[y,x]:
            zz=1-z;j=zz*wh+y*w+x;v=dist[i]+45+3*penalty[zz,y,x]
            if v<dist[j]:dist[j]=v;prev[j]=i;heapq.heappush(q,(v+heuristic[y,x],j))
        if steps>1400000:break
    path=[]
    if end>=0:
        while end>=0:
            path.append(end)
            if dist[end]==0:break
            end=prev[end]
    return path,steps

def clusters(netid):
    if '--exact-ground' in sys.argv and netid==IDS['GND']:
        from check_geometry import load_objects
        from shapely.strtree import STRtree
        objects=[o for o in load_objects() if o['net']=='GND']
        parents=list(range(len(objects)))
        def root(i):
            while parents[i]!=i:parents[i]=parents[parents[i]];i=parents[i]
            return i
        for layer in [1,2]:
            ix=[i for i,o in enumerate(objects) if layer in o['layers']]
            tree=STRtree([objects[i]['g'] for i in ix])
            for i in ix:
                for k in tree.query(objects[i]['g'].buffer(.0001)):
                    j=ix[int(k)]
                    if j>i and objects[i]['g'].distance(objects[j]['g'])<.0001:parents[root(i)]=root(j)
        live={root(i) for i,o in enumerate(objects) if o['pad']}
        images=[Image.new('I',(W,H),0),Image.new('I',(W,H),0)]
        for i,o in enumerate(objects):
            r=root(i)
            if r not in live:continue
            mask=Image.new('1',(W,H),0);d=ImageDraw.Draw(mask)
            d.polygon([pix(*q) for q in o['g'].exterior.coords],fill=1)
            for h in o['g'].interiors:d.polygon([pix(*q) for q in h.coords],fill=0)
            for layer in o['layers']:images[layer-1].paste(r+1,mask=mask)
        labs=np.stack([np.array(im,dtype=np.int32) for im in images])
        keys,count=np.unique(labs,return_counts=True)
        return labs,sorted([(int(c),int(k)) for k,c in zip(keys,count) if k])
    labs=[];sizes=[];offset=0
    for z in range(2):
        a,n=label(board[z]==netid,np.ones((3,3),np.uint8));a[a>0]+=offset;offset+=n;labs.append(a)
    labs=np.stack(labs);parents=list(range(offset+1))
    def root(i):
        while parents[i]!=i:parents[i]=parents[parents[i]];i=parents[i]
        return i
    for g in GEO:
        if 'diameter' not in g or IDS[g['net']]!=netid:continue
        x,y=pix(g['x'],g['y']);a,b=int(labs[0,y,x]),int(labs[1,y,x])
        if a and b:parents[root(a)]=root(b)
    mapping=np.array([root(i) for i in range(offset+1)],dtype=np.int32)
    labs=mapping[labs]
    keys,count=np.unique(labs,return_counts=True)
    return labs,sorted([(int(c),int(k)) for k,c in zip(keys,count) if k])

def save():
    base=json.loads((CAD/'ESP-HI-C3-RevA-PCB-placement.json').read_text())
    for i,g in enumerate(GEO):
        fmt=lambda n:f'{n/UNIT:.6f}'
        if 'points' in g:
            s=f'TRACK~{fmt(g["width"])}~{g["layer"]}~{g["net"]}~'+' '.join(fmt(v) for pt in g['points'] for v in pt)+f'~ggeFIN{i}~0'
        else:s=f'VIA~{fmt(g["x"])}~{fmt(g["y"])}~{fmt(g["diameter"])}~{g["net"]}~{fmt(g["drill"]/2)}~ggeFIN{i}~0'
        base['shape'].append(s)
    base['DRCRULE']['Default'].update(trackWidth=.125/UNIT,clearance=.125/UNIT,viaHoleDiameter=.45/UNIT,viaHoleD=.2/UNIT)
    base['head']['c_para']['name']='ESP-HI-C3-RevA routed review draft'
    for name,data in [('routed-geometry.json',GEO),('ESP-HI-C3-RevA-PCB-routed.json',base)]:
        dst=CAD/name;tmp=CAD/(name+'.tmp')
        tmp.write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
        for attempt in range(6):
            try:os.replace(tmp,dst);break
            except OSError:
                if attempt==5:raise
                time.sleep(.2)

def commit_path(path,net,width=.15):
    """Commit a grid path using real copper tracks and plated vias."""
    global board
    nid=IDS[net];coords=[]
    for i in reversed(path):coords.append((i//(H*W),(i%(H*W))//W,i%W))
    pieces=[];part=[]
    for z,y,x in coords:
        if part and z!=part[-1][0]:
            pieces.append(part);part=[]
            v=dict(net=net,x=x*STEP,y=y*STEP,diameter=.3 if '--micro-vias' in sys.argv else .45,drill=.15 if '--micro-vias' in sys.argv else .2)
            GEO.append(v)
            for iz in range(2):draw_geo(ims[iz],v,nid)
        part.append((z,y,x))
    if part:pieces.append(part)
    for p in pieces:
        if len(p)<2:continue
        points=[(p[0][2]*STEP,p[0][1]*STEP)]
        for j in range(1,len(p)-1):
            a,b,c=p[j-1],p[j],p[j+1]
            if (b[1]-a[1],b[2]-a[2])!=(c[1]-b[1],c[2]-b[2]):points.append((b[2]*STEP,b[1]*STEP))
        points.append((p[-1][2]*STEP,p[-1][1]*STEP))
        g=dict(net=net,layer=p[0][0]+1,width=width,points=points)
        GEO.append(g);draw_geo(ims[g['layer']-1],g,nid)
    board=np.stack([np.array(im,dtype=np.int32) for im in ims])

def prune_orphans():
    """Remove wire fragments that no longer touch any physical component land."""
    from check_geometry import load_objects
    from shapely.strtree import STRtree
    objects=load_objects();parents=list(range(len(objects)))
    def root(i):
        while parents[i]!=i:parents[i]=parents[parents[i]];i=parents[i]
        return i
    for layer in [1,2]:
        ix=[i for i,o in enumerate(objects) if layer in o['layers']]
        tree=STRtree([objects[i]['g'] for i in ix])
        for i in ix:
            a=objects[i]
            for k in tree.query(a['g'].buffer(.0001)):
                j=ix[int(k)];b=objects[j]
                if i<j and a['net']==b['net'] and a['g'].distance(b['g'])<.0001:parents[root(i)]=root(j)
    live={root(i) for i,o in enumerate(objects) if o['pad']}
    remove=set()
    for i,o in enumerate(objects):
        if root(i) in live or not o['name'].startswith(('route','via')):continue
        k=int(o['name'].removeprefix('route').removeprefix('via'))
        if k>=7:remove.add(k)
    if remove:
        GEO[:]=[g for i,g in enumerate(GEO) if i not in remove];save()
    print('Pruned',len(remove),'orphan copper objects',flush=True)

if '--prune' in sys.argv:
    prune_orphans();save();sys.exit(0)

if '--fresh' in sys.argv:
    # Escape fine-pitch package pins before long connections can fence them in.
    candidates=[p for p in PADS if p['ref'] in ('U1','U5','J1','J2') and p['net']
                and p['net'] not in ('GND','RF_CHIP','RF_50','XTAL_P','XTAL_N','XTAL_LOAD')]
    candidates += [p for p in PADS if p['ref'] not in ('U1','U5','J1','J2') and p['net']
                   and min(p['w'],p['h'])<=.65
                   and p['net'] not in ('GND','RF_CHIP','RF_50','XTAL_P','XTAL_N','XTAL_LOAD')]
    for p in candidates:
        net=p['net'];nid=IDS[net];x,y=pix(p['x'],p['y'])
        labs,groups=clusters(nid);group=labs[0,y,x]
        if group and np.any(labs[1]==group):continue
        distances=np.stack([distance_transform_edt((board[z]==0)|(board[z]==nid)) for z in range(2)])
        free=(distances>=(.205 if '--fine-grid' in sys.argv else .215)/STEP)|(board==nid)
        free[:,:8,:]=False;free[:,-8:,:]=False;free[:,:,:8]=False;free[:,:,-8:]=False
        via_ok=np.all(distances>=(.295 if '--micro-vias' in sys.argv else .365)/STEP,axis=0)&via_pad_ok
        # Keep fine-pitch fanout vias on each pin's exact row/column. A one-grid
        # sideways drift can otherwise close the adjacent 0.5 mm pitch escape.
        aligned=np.zeros((H,W),dtype=np.bool_)
        if p['ref']=='U1':
            n=int(p['pin'])
            if 9<=n<=16 or 25<=n<=32:aligned[:,x]=True
            else:aligned[y,:]=True
        elif p['ref'] in ('J1','J2'):aligned[:,x]=True
        elif p['ref']=='U5':aligned[y,:]=True
        else:aligned[:,:]=True
        via_ok &= aligned
        target=np.zeros(SHAPE,dtype=np.bool_);target[1]=via_ok
        source=np.zeros(SHAPE,dtype=np.bool_);source[0,y,x]=True
        heuristic=distance_transform_edt(~via_ok)
        path,steps=search(free,via_ok,source,target,heuristic,np.zeros(SHAPE,np.float32))
        if path:commit_path(path,net)
        else:print('FANOUT FAILED',p['ref'],p['pin'],net,steps,flush=True)
    save()
    print('Fine-pitch fanout complete',flush=True)

if '--ripup' in sys.argv:
    # Permit a local reroute across removable tracks, while component lands,
    # crystal/RF geometry and holes remain hard obstacles. Exact geometry
    # identifies each displaced object before the replacement is committed.
    from check_geometry import load_objects
    from shapely.geometry import LineString,Point
    net=sys.argv[sys.argv.index('--ripup')+1];nid=IDS[net]
    labs,groups=clusters(nid)
    if len(groups)<2:sys.exit('Already connected on grid: '+net)
    source=labs==groups[0][1];target=(labs>0)&~source
    hi=[Image.new('I',(W,H),0),Image.new('I',(W,H),0)]
    for p in PADS:draw_geo(hi[0],p,IDS.get(p['net'],999))
    for g in [a for i,a in enumerate(GEO) if i<7 or a.get('protected')]:
        for z in ([g['layer']-1] if 'points' in g else [0,1]):draw_geo(hi[z],g,IDS[g['net']])
    for s in pcb['shape']:
        for t in s.split('#@$'):
            if t.startswith('HOLE~'):
                a=t.split('~');x,y,r=[float(v)*UNIT for v in a[1:4]]
                for im in hi:draw_geo(im,dict(x=x,y=y,diameter=2*(r+.175)),999)
    hb=np.stack([np.array(im,dtype=np.int32) for im in hi])
    hd=np.stack([distance_transform_edt((hb[z]==0)|(hb[z]==nid)) for z in range(2)])
    sd=np.stack([distance_transform_edt((board[z]==0)|(board[z]==nid)) for z in range(2)])
    free=(hd>=.205/STEP)|(hb==nid)|source|target
    if net=='GND':
        free=hd>=.215/STEP
        source&=free;target&=free
    free[:,:8,:]=False;free[:,-8:,:]=False;free[:,:,:8]=False;free[:,:,-8:]=False
    via_ok=np.all(hd>=(.295 if '--micro-vias' in sys.argv else .39)/STEP,axis=0)&via_pad_ok
    penalty=np.where(sd<.205/STEP,35.,0.).astype(np.float32)
    history_path=ROOT.parents[1]/'.local/pcb-routing-history.npy'
    history=np.load(history_path) if history_path.exists() else np.zeros(SHAPE,np.float32)
    if history.shape!=SHAPE:history=np.zeros(SHAPE,np.float32)
    penalty+=history
    heuristic=distance_transform_edt(~np.any(target,axis=0))
    path,steps=search(free,via_ok,source,target,heuristic,penalty)
    if not path:sys.exit('No hard-legal ripup path: '+net)
    old=len(GEO);commit_path(path,net,width=.125)
    replacement=GEO[old:];objects=load_objects();remove=set()
    for g in replacement:
        gg=LineString(g['points']).buffer(g['width']/2) if 'points' in g else Point(g['x'],g['y']).buffer(g['diameter']/2)
        layers=[g['layer']] if 'points' in g else [1,2]
        for o in objects:
            if o['name'].startswith('plane'):continue
            if o['net']==net or not set(layers)&set(o['layers']):continue
            if gg.distance(o['g'])>=.124:continue
            if o['pad'] or o['name'].startswith('gge'):raise RuntimeError('Candidate would hit fixed geometry: '+o['name'])
            index=int(o['name'].removeprefix('route').removeprefix('via'))
            if index<7 or GEO[index].get('protected'):raise RuntimeError('Candidate would hit protected route')
            remove.add(index)
    print('Local ripup',net,'displaced',len(remove),'objects',sorted({GEO[i]['net'] for i in remove}),flush=True)
    if net=='GND':
        for g in replacement:g.update(protected=True,purpose='ground_return')
    congestion=[Image.new('I',(W,H),0),Image.new('I',(W,H),0)]
    for i in remove:
        g=GEO[i]
        for z in ([g['layer']-1] if 'points' in g else [0,1]):draw_geo(congestion[z],g,1)
    for z in range(2):
        near=distance_transform_edt(np.array(congestion[z])==0)<.3/STEP
        history[z,near]+=5
    np.save(history_path,history)
    GEO[:]=[g for i,g in enumerate(GEO) if i not in remove]
    save();prune_orphans();sys.exit(0)

new=0;failures=[]
# Route short analog/local connections before long bus runs. Preserve ground
# capacity by leaving GND to the verified copper-pour/stitching stage.
pending=[]
for net in NETS:
    if (net=='GND') != ('--ground' in sys.argv):continue
    labs,groups=clusters(IDS[net])
    if len(groups)>1:
        core=any(p['ref']=='U1' and p['net']==net for p in PADS)
        power=net in ['+3V4','VBUS','+3V0_LCD']
        pending.append(((2 if power else 0 if core else 1)*100000+sum(x[0] for x in groups),net))
for _,net in sorted(pending):
    nid=IDS[net];tries=0
    while True:
        labs,groups=clusters(nid)
        if len(groups)<=1:break
        tries+=1
        if tries>100:failures.append(net);break
        distances=np.stack([distance_transform_edt((board[z]==0)|(board[z]==nid)) for z in range(2)])
        thin=net=='LCD_SCLK' and '--thin-clock' in sys.argv
        free=(distances>=(.215 if net=='GND' else .19 if thin else .205 if '--fine-grid' in sys.argv else .215)/STEP)
        if net!='GND':free|=(board==nid)
        # Keep all new copper at least 0.30 mm from the board outline.
        free[:,:8,:]=False;free[:,-8:,:]=False;free[:,:,:8]=False;free[:,:,-8:]=False
        via_ok=np.all(distances>=(.295 if '--micro-vias' in sys.argv else .365)/STEP,axis=0)&via_pad_ok
        path=[];steps=0
        # An enclosed ground island must not stop the other pad groups from
        # reaching the main plane. Try every source before requesting ripup.
        for _,group_id in groups[:-1] if net=='GND' else groups[:1]:
            source=labs==group_id;target=(labs>0)&~source
            if net=='GND':source&=free;target&=free
            heuristic=distance_transform_edt(~np.any(target,axis=0))
            path,steps=search(free,via_ok,source,target,heuristic,np.zeros(SHAPE,np.float32))
            if path:break
        if not path:
            failures.append(net);print('NO PATH',net,'groups',len(groups),'visited',steps,flush=True);break
        path=list(reversed(path));coords=[(i//(H*W),(i%(H*W))//W,i%W) for i in path]
        pieces=[];part=[]
        for z,y,x in coords:
            if part and z!=part[-1][0]:
                pieces.append(part);part=[]
                v=dict(net=net,x=x*STEP,y=y*STEP,diameter=.3 if '--micro-vias' in sys.argv else .45,drill=.15 if '--micro-vias' in sys.argv else .2)
                GEO.append(v)
                for iz in range(2):draw_geo(ims[iz],v,nid)
            part.append((z,y,x))
        if part:pieces.append(part)
        for p in pieces:
            if len(p)<2:continue
            points=[(p[0][2]*STEP,p[0][1]*STEP)]
            for j in range(1,len(p)-1):
                a,b,c=p[j-1],p[j],p[j+1]
                if (b[1]-a[1],b[2]-a[2])!=(c[1]-b[1],c[2]-b[2]):points.append((b[2]*STEP,b[1]*STEP))
            points.append((p[-1][2]*STEP,p[-1][1]*STEP))
            g=dict(net=net,layer=p[0][0]+1,width=.1 if thin else .125 if '--fine-grid' in sys.argv else .15,points=points)
            GEO.append(g);draw_geo(ims[g['layer']-1],g,nid)
        board=np.stack([np.array(im,dtype=np.int32) for im in ims]);new+=1
        print('CONNECTED',net,'remaining groups',len(groups)-1,'visited',steps,flush=True)
        save()
save()
report={'new_connections':new,'failed_nets':sorted(set(failures)),'remaining':{}}
for net in NETS:
    _,groups=clusters(IDS[net])
    if len(groups)>1:report['remaining'][net]=len(groups)-1
(ROOT/'evidence/grid-routing.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report),flush=True)
