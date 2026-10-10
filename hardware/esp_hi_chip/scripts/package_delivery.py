"""Check and package staged hardware files exactly as Git will publish them.

Run from any directory after staging this hardware folder. No files are deleted.
The snapshot intentionally excludes untracked/ignored research and license data.
"""
from pathlib import Path
import ast, csv, hashlib, io, json, re, subprocess, sys, zipfile

ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[1]
sys.path.append(str(REPO/'.local/pcb-python'))
import pymupdf
def git(*args):
    return subprocess.check_output(['git','-C',str(REPO),*args])
paths=[p.decode() for p in git('ls-files','-z','--','hardware/esp_hi_chip').split(b'\0') if p]
files={str(Path(p).relative_to('hardware/esp_hi_chip')).replace('\\','/'):git('show',':'+p)
       for p in paths if not p.endswith('/SHA256SUMS.txt') and not p.endswith('/evidence/DELIVERY_CHECK.json')}
assert files,'Stage hardware files before packaging.'
for name,content in files.items():
    if name.endswith('.py'):ast.parse(content.decode('utf-8-sig'),filename=name)
checks={}
for name in ['geometry-check','manufacturing-geometry','gerber-export-check','native-contract-check',
             'schematic-netlist-contract','pcb-netlist-contract']:
    data=json.loads(files['evidence/'+name+'.json'].decode('utf-8-sig'))
    assert data['status']=='PASS',name
    checks[name]='PASS'
export=json.loads(files['evidence/pro-export-final.json'].decode('utf-8-sig'))
assert export['ok'] and export['value']['nativeDrc']==[]
gerber=json.loads(files['evidence/gerber-export-check.json'].decode('utf-8-sig'))
for name,key in [('prototype-gerber','native_zip_sha256'),('fabrication','fabrication_zip_sha256')]:
    assert hashlib.sha256(files[f'exports/ESP-HI-C3-RevA-{name}.zip']).hexdigest()==gerber[key]
pdf=pymupdf.open(stream=files['exports/ESP-HI-C3-RevA-schematic.pdf'],filetype='pdf')
assert len(pdf)==7
bom=list(csv.DictReader(io.StringIO(files['exports/ESP-HI-C3-RevA-BOM.csv'].decode('utf-8-sig'))))
placement=list(csv.DictReader(io.StringIO(files['exports/ESP-HI-C3-RevA-placement-fitted.csv'].decode('utf-8-sig'))))
fitted={r['Designator'] for r in bom if r['Assembly']=='FIT'}
assert len(bom)==109 and len(fitted)==101 and {r['Designator'] for r in placement}==fitted
assert all(r['Layer']=='T' for r in placement)
badlinks=[]
for name,content in files.items():
    if not name.endswith('.md'):continue
    for target in re.findall(r'\]\(([^)]+)\)',content.decode('utf-8-sig')):
        if '://' in target or target.startswith('#'):continue
        rel=(ROOT/Path(name).parent/target.split('#')[0]).resolve().relative_to(ROOT).as_posix()
        if rel not in files:badlinks.append([name,target])
assert not badlinks,badlinks
report=dict(status='PASS',date='2026-10-10',checks=checks,native_drc_errors=0,schematic_pages=len(pdf),
            footprint_positions=109,fitted_parts=101,dnp_parts=['C12','C13'],bare_test_pads=6,
            placement_rows=len(placement),all_fitted_parts_top=True,markdown_links='PASS',
            python_syntax='PASS',actual_gerber_hashes='PASS',physical_validation='NOT RUN')
report_bytes=(json.dumps(report,indent=2)+'\n').encode()
(ROOT/'evidence/DELIVERY_CHECK.json').write_bytes(report_bytes)
files['evidence/DELIVERY_CHECK.json']=report_bytes
manifest=''.join(hashlib.sha256(data).hexdigest()+'  '+name+'\n' for name,data in sorted(files.items()))
(ROOT/'SHA256SUMS.txt').write_text(manifest,encoding='utf-8',newline='\n')
files['SHA256SUMS.txt']=manifest.encode()
snapshot=REPO/'.local/hardware-delivery-20261010'
for name,data in files.items():
    dest=snapshot/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
out=REPO/'outputs/ESP-HI-C3-RevA-hardware-package.zip';out.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as archive:
    for name,data in sorted(files.items()):
        info=zipfile.ZipInfo('ESP-HI-C3-RevA/'+name,date_time=(2026,10,10,0,0,0))
        info.compress_type=zipfile.ZIP_DEFLATED;archive.writestr(info,data)
print(json.dumps(dict(status='PASS',files=len(files),zip=str(out),bytes=out.stat().st_size,
                     sha256=hashlib.sha256(out.read_bytes()).hexdigest(),snapshot=str(snapshot)),indent=2))
