"""Bounded ground completion with signal recovery and exact checks per pass."""
import json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def run(script,*args):
    p=subprocess.run([sys.executable,'-X','utf8',str(ROOT/'scripts'/script),*args],capture_output=True,text=True)
    print(p.stdout[-2200:],p.stderr[-700:],flush=True)
    return p.returncode
flags=['--fine-grid','--micro-vias']
for i in range(int(sys.argv[1]) if len(sys.argv)>1 else 6):
    print('GROUND PASS',i,flush=True)
    run('pour_ground.py')
    run('finish_routes.py',*flags,'--ground','--exact-ground')
    run('check_geometry.py')
    report=json.loads((ROOT/'evidence/geometry-check.json').read_text())
    if report['status']=='PASS':break
    if 'GND' not in report['unconnected_nets']:break
    if run('finish_routes.py',*flags,'--ground','--exact-ground','--ripup','GND'):break
    run('finish_routes.py',*flags)
    run('route_iterations.py','4',*flags)
    run('repair_geometry.py')
    run('pour_ground.py')
    run('check_geometry.py')
    report=json.loads((ROOT/'evidence/geometry-check.json').read_text())
    if any(n!='GND' for n in report['unconnected_nets']):break
