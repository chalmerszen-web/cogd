"""Read the actual LCEDA manufacturing export, verify drills, and render layers.

Requires gerbonara and PyMuPDF. The production ZIP preserves native layer bytes;
only auxiliary/duplicate files are omitted. It does not rewrite CAM geometry.
"""
from pathlib import Path
from collections import Counter
import argparse, hashlib, json, sys, zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.append(str(ROOT.parents[1] / '.local/pcb-python'))
from gerbonara.rs274x import GerberFile
from gerbonara.excellon import ExcellonFile
from gerbonara.utils import MM
import pymupdf

parser = argparse.ArgumentParser()
parser.add_argument('zip', type=Path)
args = parser.parse_args()
out = ROOT / 'exports'
out.mkdir(exist_ok=True)
review = out / 'gerber-review'
review.mkdir(exist_ok=True)
with zipfile.ZipFile(args.zip) as archive:
    files = {Path(n).name: archive.read(n) for n in archive.namelist() if not n.endswith('/')}

layers = {n: GerberFile.from_string(b.decode('utf-8-sig'), filename=n)
          for n, b in files.items() if n.startswith('Gerber_')}
drills = {n: ExcellonFile.from_string(b.decode('utf-8-sig'), filename=n,
                                  plated='NPTH' not in n)
          for n, b in files.items() if n.endswith('.DRL')}

def holes(file):
    return sorted((round(MM(h.x, h.unit), 5), round(MM(h.y, h.unit), 5),
                   round(h.tool.equivalent_width(MM), 5)) for h in file.drills())

pth = holes(drills['Drill_PTH_Through.DRL'])
npth = holes(drills['Drill_NPTH_Through.DRL'])
duplicate = pth == holes(drills['Drill_PTH_Through_Via.DRL'])
geo = json.loads((ROOT/'cad/routed-geometry.json').read_text())
intended = sorted((round(a['x'], 5), round(-a['y'], 5), round(a['drill'], 5))
                  for a in geo if 'diameter' in a)
drill_match = len(pth) == len(intended) and all(
    all(abs(x-y) <= .0001 for x, y in zip(a, b)) for a, b in zip(pth, intended))
outline = layers['Gerber_BoardOutlineLayer.GKO']
vertices = {(round(MM(getattr(a, x), a.unit), 5), round(MM(getattr(a, y), a.unit), 5))
            for a in outline.objects for x,y in [('x1','y1'),('x2','y2')]}
outline_size = [round(max(p[i] for p in vertices)-min(p[i] for p in vertices), 5) for i in [0,1]]
mask_empty = not layers['Gerber_BottomSolderMaskLayer.GBS'].objects

selected = ['Gerber_TopLayer.GTL', 'Gerber_BottomLayer.GBL',
            'Gerber_TopSolderMaskLayer.GTS', 'Gerber_BottomSolderMaskLayer.GBS',
            'Gerber_TopPasteMaskLayer.GTP', 'Gerber_BoardOutlineLayer.GKO']
pdf = pymupdf.open()
for name in selected:
    svg = str(layers[name].to_svg(force_bounds=((-1,-47.35),(35.30,1)), fg='#172a39', bg='white'))
    (review / (name+'.svg')).write_text(svg, encoding='utf-8')
    source = pymupdf.open(stream=svg.encode(), filetype='svg')
    pagepdf = pymupdf.open(stream=source.convert_to_pdf(), filetype='pdf')
    pagepdf[0].get_pixmap(matrix=pymupdf.Matrix(7,7)).save(review/(name+'.png'))
    pdf.insert_pdf(pagepdf)
pdf.save(out/'ESP-HI-C3-RevA-Gerber-layer-review.pdf')

assert duplicate, 'Do not omit the auxiliary drill file unless it is an exact geometry duplicate.'
keep = [n for n in files if n.endswith(('.GTL','.GBL','.GTO','.GBO','.GTS','.GBS','.GTP','.GKO'))]
keep += ['Drill_PTH_Through.DRL','Drill_NPTH_Through.DRL']
fab = out/'ESP-HI-C3-RevA-fabrication.zip'
with zipfile.ZipFile(fab,'w',zipfile.ZIP_DEFLATED) as archive:
    for name in sorted(keep):
        info=zipfile.ZipInfo(name, date_time=(2026,10,10,0,0,0))
        info.compress_type=zipfile.ZIP_DEFLATED
        archive.writestr(info, files[name])

report = {
    'status': 'PASS' if duplicate and drill_match and mask_empty and outline_size==[34.3,46.35] and len(npth)==2 else 'FAIL',
    'native_zip_sha256': hashlib.sha256(args.zip.read_bytes()).hexdigest(),
    'fabrication_zip_sha256': hashlib.sha256(fab.read_bytes()).hexdigest(),
    'outline_centerline_mm': outline_size,
    'copper_layers': ['top','bottom'],
    'plated_drills': len(pth),
    'plated_drill_diameters_mm': dict(Counter(h[2] for h in pth)),
    'npth': npth,
    'plated_drills_match_canonical_geometry': drill_match,
    'auxiliary_via_drill_is_exact_duplicate': duplicate,
    'bottom_soldermask_openings': len(layers['Gerber_BottomSolderMaskLayer.GBS'].objects),
    'native_layer_objects': {n:len(f.objects) for n,f in layers.items()},
    'fabrication_zip_files': sorted(keep),
    'omitted_auxiliary_files': sorted(set(files)-set(keep)),
    'note': 'Native Gerber geometry preserved byte-for-byte. This is an export integrity check, not an electrical simulation or physical validation.'
}
(ROOT/'evidence/gerber-export-check.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
if report['status'] != 'PASS':
    raise SystemExit(1)
