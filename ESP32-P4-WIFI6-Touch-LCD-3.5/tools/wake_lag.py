"""Read-only development diagnosis of verifier support age at accepted events."""
import argparse,json
from pathlib import Path
from wake_host import ROOT,Oracle

def main():
    p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    oracle=Oracle(a.library);rows=[]
    for r in map(json.loads,a.manifest.read_text(encoding='utf8').splitlines()):
        assert r['split']!='test'
        scores,events=oracle.wav(ROOT/r['path']);votes=0;last=None;ages={}
        for i,value in enumerate(scores[:,4]):
            votes=((votes<<1)|int(value>=268))&7
            if i>=63 and votes.bit_count()>=2:last=i
            ages[(i+1)*512-40960]=None if last is None else (i-last)*32
        result={'clip_id':r['clip_id'],'label':r['label'],'group':r.get('wake_group',r['language']),
                'events':[{'sample':e,'support_age_ms':ages[e],'scores':scores[(e+40960)//512-1,3:].tolist()} for e in events]}
        rows.append(result)
    a.out.write_text(json.dumps(rows,indent=2),encoding='utf8')
    print(json.dumps([r for r in rows if not r['label'] and r['events']]))

if __name__=='__main__':main()
