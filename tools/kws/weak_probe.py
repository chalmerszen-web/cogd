"""Two fixed attenuation diagnostics, explicitly not a five-metre claim."""
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/kws-phase5'


def main():
    formal=json.loads((OUT/'near-formal/replay.json').read_text(encoding='utf8'))
    assert formal['complete'] and not formal['resources']['failures']
    rows=json.loads((OUT/'near-formal/selection.json').read_text(encoding='utf8'))
    selected=[]
    for language in ('zh','yue'):
        selected.extend([r for r in rows if r['label'] and r['language']==language][:4])
    negatives=[r for r in rows if not r['label']]
    selected.extend(negatives[i] for i in (0,1,2,3,7,8,9,10,15,16))
    manifest=OUT/'weak-selection.jsonl'
    with manifest.open('x',encoding='utf8') as f:
        for row in selected:f.write(json.dumps(row,ensure_ascii=False)+'\n')
    plan=dict(attenuation_db=[6,12],baseline_gain=.35,per_language=4,negatives=10,
              model='adapt-b',five_metre_equivalence=False)
    (OUT/'weak-plan.json').write_text(json.dumps(plan,indent=2))
    results=[]
    for db in plan['attenuation_db']:
        directory=OUT/f'weak-minus{db:02d}'
        command=[sys.executable,'-X','utf8',str(ROOT/'tools/kws/acoustic_test.py'),'replay',
            '--manifest',str(manifest),'--threshold',str(OUT/'adapt-b-int8/threshold.json'),
            '--out',str(directory),'--per-language','4','--negatives','10','--device','0',
            '--gain',str(.35*10**(-db/20)),'--source-rms','.14','--meter']
        with (OUT/f'weak-minus{db:02d}.log').open('xb') as f:
            result=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT)
        if result.returncode:raise RuntimeError('Weak probe failed; inspect preserved evidence')
        report=json.loads((directory/'replay.json').read_text(encoding='utf8'))
        assert report['complete'] and all(t['observed_after_playback_ms']>=1200 for t in report['trials'])
        results.append(dict(attenuation_db=db,languages=report['languages'],negatives=report['negatives'],resources=report['resources']))
        (OUT/'weak-results.json').write_text(json.dumps(results,indent=2))
        print(json.dumps(results[-1]),flush=True)


if __name__=='__main__':main()
