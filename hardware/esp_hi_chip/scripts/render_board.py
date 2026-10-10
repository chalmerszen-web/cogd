"""Readable copper, component and display-envelope review drawings."""
from pathlib import Path
import json,math
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon as PatchPolygon,Rectangle,PathPatch
from matplotlib.path import Path as PlotPath
from check_geometry import load_objects
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'exports';OUT.mkdir(exist_ok=True)
objects=load_objects();poses=json.loads((ROOT/'cad/placement.json').read_text())
model=json.loads((ROOT/'design-intent.json').read_text());fps=json.loads((ROOT/'libraries/footprints-mm.json').read_text())
def polygon_patch(g,**style):
    vertices=[];codes=[]
    for ring in [g.exterior,*g.interiors]:
        coords=list(ring.coords);vertices+=coords;codes += [PlotPath.MOVETO]+[PlotPath.LINETO]*(len(coords)-2)+[PlotPath.CLOSEPOLY]
    return PathPatch(PlotPath(vertices,codes),**style)
fig,axes=plt.subplots(1,3,figsize=(15,8.2),layout='constrained')
for ax,layer in zip(axes[:2],[1,2]):
    ax.add_patch(Rectangle((0,0),34.3,46.35,facecolor='#edf4ef',edgecolor='#30423a',linewidth=1.5))
    for a in objects:
        if layer not in a['layers']:continue
        color=('#c5d9c9' if a['name'].startswith('plane') else '#b77423' if a['pad'] else '#db675c' if layer==1 else '#4f80c1')
        if a['net'].startswith('@HOLE'):color='white'
        ax.add_patch(polygon_patch(a['g'],facecolor=color,edgecolor='none',alpha=1))
    if layer==1:
        for part in model['components']:
            ref=part['ref'];x,y,angle=poses[ref];fp=fps[part['footprint']];a,b,c,d=fp['bbox'];t=math.radians(angle)
            ps=[(x+u*math.cos(t)-v*math.sin(t),y+u*math.sin(t)+v*math.cos(t)) for u,v in [(a,b),(c,b),(c,d),(a,d)]]
            ax.add_patch(PatchPolygon(ps,closed=True,fill=False,edgecolor='#142820',linewidth=.45))
            ax.text(x,y,ref,ha='center',va='center',fontsize=4.7,color='#101820',bbox=dict(facecolor='white',edgecolor='none',pad=.2,alpha=.85))
    ax.add_patch(Rectangle((1.5,37),31.3,9.35,fill=False,edgecolor='#725dac',linestyle='--',linewidth=.9))
    ax.text(17.15,41.9,'FPC assembly space\nNo components',ha='center',va='center',fontsize=8,color='#59457b')
    ax.set(xlim=(-.9,35.2),ylim=(47.2,-2),aspect='equal',xlabel='mm',ylabel='mm',title='Top: components + copper' if layer==1 else 'Bottom copper (through-board view)')
    ax.tick_params(labelsize=8);ax.grid(False)
ax=axes[2]
ax.add_patch(Rectangle((-.2,-.2),34.7,46.75,facecolor='#172537',edgecolor='#172537'))
ax.add_patch(Rectangle((3.2,4.8),28.03,35.04,facecolor='#83a6aa',edgecolor='#bfd7d9'))
ax.add_patch(Rectangle((0,0),34.3,46.35,fill=False,edgecolor='#ffc45c',linewidth=1.4,linestyle='--'))
ax.text(17.15,18,'NHD-1.8\n128 x 160 SPI',ha='center',va='center',fontsize=13,color='white')
ax.text(17.15,28,'Display bonding face\nNo bottom components',ha='center',va='center',fontsize=10,color='#172537')
ax.annotate('',(0,49),(34.3,49),arrowprops=dict(arrowstyle='<->',color='#263a49'))
ax.text(17.15,50,'PCB 34.30 mm',ha='center',va='top',fontsize=9)
ax.annotate('',(37,0),(37,46.35),arrowprops=dict(arrowstyle='<->',color='#263a49'))
ax.text(38,23.18,'PCB 46.35 mm',rotation=90,ha='center',va='center',fontsize=9)
ax.set(xlim=(-1.8,41),ylim=(53,-2),aspect='equal',title='Screen / PCB envelope');ax.axis('off')
fig.suptitle('ESP-HI C3 CHIP / REV A / 2 layers / 1.0 mm nominal / 109 top-side footprints',fontsize=13)
fig.savefig(OUT/'ESP-HI-C3-RevA-board-review.png',dpi=180)
fig.savefig(OUT/'ESP-HI-C3-RevA-board-review.svg')
fig.savefig(OUT/'ESP-HI-C3-RevA-board-review.pdf')
print('Board review PNG / SVG / PDF written')
