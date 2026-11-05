"""DEVELOPMENT-only cached C-policy curve, including unsupported low settings.

The device currently permits500..950. A lower analysis point is not firmware
validation and never changes the board, a source recording, or a model.
"""
import argparse,ctypes as C,json,hashlib
from pathlib import Path
import numpy as np

def main():
    p=argparse.ArgumentParser();p.add_argument('--curve',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    source=a.curve/'report.json';old=json.loads(source.read_text(encoding='utf8'));assert old['complete'] and 'DEVELOPMENT' in old['scope']
    dll=C.CDLL(str((a.curve/'host/libwake.so').resolve()));dll.decision_reset.argtypes=[C.c_int]
    dll.decision_step.argtypes=[C.c_int,C.c_int,C.c_uint64];dll.decision_step.restype=C.c_int
    rows=[(r,np.load(a.curve/(r['id']+'.npz'),allow_pickle=False)['scores']) for r in old['trials']]
    result={'scope':'Cached DEVELOPMENT only; settings below500 require explicit firmware support and future physical validation',
            'curve_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'thresholds':{},'passing':[]}
    for setting in [1,5,*range(10,951,10)]:
        q=int(np.floor(np.log(setting/(1000-setting))*256+.5));groups={}
        for row,scores in rows:
            dll.decision_reset(q);events=[(i+1)*512 for i,s in enumerate(scores) if dll.decision_step(int(s[3]),int(s[4]),32768+(i+1)*512)]
            lo,hi=row['accept_bounds'];hit=bool(row['label']) and any(lo<=e<=hi for e in events)
            false=any(not row['label'] or not lo<=e<=hi for e in events)
            g=groups.setdefault(row['group'],{'count':0,'hits':0,'false':0});g['count']+=1;g['hits']+=int(hit);g['false']+=int(false)
        passed=all((g['hits']/g['count']>=.95 if k!='negative' else True) and not g['false'] for k,g in groups.items())
        result['thresholds'][str(setting)]={'groups':groups,'passed':passed}
        if passed:result['passing'].append(setting)
    assert not a.out.exists();a.out.write_text(json.dumps(result,indent=2),encoding='utf8');print(json.dumps({'curve':a.curve.name,'passing':result['passing']}))

if __name__=='__main__':main()
