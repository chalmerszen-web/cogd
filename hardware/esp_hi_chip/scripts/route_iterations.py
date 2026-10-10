"""Bounded local ripup iterations. Does not assert a native DRC result."""
import json,subprocess,sys,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
script=ROOT/'scripts/finish_routes.py';flags=sys.argv[2:]
cool={};best=10000;events=[]
for iteration in range(int(sys.argv[1]) if len(sys.argv)>1 else 20):
    report=json.loads((ROOT/'evidence/grid-routing.json').read_text())
    remaining={n:v for n,v in report['remaining'].items() if n!='GND'}
    score=sum(remaining.values())
    if score<best:
        best=score
        for n in ['routed-geometry.json','ESP-HI-C3-RevA-PCB-routed.json']:
            shutil.copy2(ROOT/'cad'/n,ROOT/'cad/attempts'/('best-'+n))
    print('Iteration',iteration,'remaining',remaining,flush=True)
    if not remaining:break
    net=min(remaining,key=lambda n:(cool.get(n,-1),-remaining[n],n))
    cool[net]=iteration
    r=subprocess.run([sys.executable,'-X','utf8',str(script),'--ripup',net]+flags,capture_output=True,text=True)
    events.append(dict(iteration=iteration,net=net,ripup=r.stdout,error=r.stderr))
    print(r.stdout.strip(),r.stderr[-500:],flush=True)
    if not r.returncode:
        r=subprocess.run([sys.executable,'-X','utf8',str(script)]+flags,capture_output=True,text=True)
        events[-1]['reroute']=r.stdout;events[-1]['reroute_error']=r.stderr
        print(r.stdout.splitlines()[-1:] or r.stderr[-500:],flush=True)
        if r.returncode:break
    (ROOT/'evidence/routing-iterations.json').write_text(json.dumps(events,indent=2)+'\n')
