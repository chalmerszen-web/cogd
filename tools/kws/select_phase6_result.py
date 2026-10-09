"""Apply preregistered live replay comparison without changing any thresholds."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]


def main():
    base=ROOT/'artifacts/kws-phase6'
    report=dict(complete=False,runs={},checks={},firmware_changed=False)
    raw={}
    for pair in ('ef','ek'):
        rows=[];raw[pair]=[]
        for db in (0,6,12):
            path=base/('replay-'+pair)/f'minus{db:02d}/replay.json'
            item=json.loads(path.read_text(encoding='utf8'))
            assert item['complete'] and len(item['trials'])==20
            assert all(t['observed_after_playback_ms']>=1200 for t in item['trials'])
            raw[pair].append(item)
            rows.append(dict(db=db,languages=item['languages'],negatives=item['negatives'],
                partial_triggers=sum(t['triggered'] for t in item['trials'] if t['text'] in ('你好','小言')),
                resources=item['resources'],report_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        report['runs'][pair]=dict(levels=rows,total_hits={lang:sum(r['languages'][lang]['valid_hits'] for r in rows)
            for lang in ('zh','yue')},negative_triggers=sum(r['negatives']['triggers'] for r in rows),
            partial_triggers=sum(r['partial_triggers'] for r in rows))
    for a,b in zip(raw['ef'],raw['ek']):
        assert a['source_files']==b['source_files']
        assert a['settings']['gain']==b['settings']['gain']
        assert [t['clip_id'] for t in a['trials']]==[t['clip_id'] for t in b['trials']]
        assert {t['output']['device_name'] for t in a['trials']}=={t['output']['device_name'] for t in b['trials']}
    old,new=report['runs']['ef'],report['runs']['ek']
    checks=report['checks']
    checks['resources']=all(not r['resources']['failures'] for r in new['levels'])
    checks['no_partial']=new['partial_triggers']==0
    checks['normal_nonregression']=all(new['levels'][0]['languages'][lang]['valid_hits']>=old['levels'][0]['languages'][lang]['valid_hits'] for lang in ('zh','yue'))
    checks['total_each_language_nonregression']=all(new['total_hits'][lang]>=old['total_hits'][lang] for lang in ('zh','yue'))
    checks['negative_nonregression']=new['negative_triggers']<=old['negative_triggers']
    checks['some_quality_improvement']=new['negative_triggers']<old['negative_triggers'] or any(new['total_hits'][lang]>old['total_hits'][lang] for lang in ('zh','yue'))
    report.update(complete=True,selected_pair='ek' if all(checks.values()) else 'ef',
        limitation='Finite sequential synthetic-speaker comparison, not an optimum proof, independent human test or physical5m result.')
    with (base/'selection-decision.json').open('x',encoding='utf8') as f:json.dump(report,f,ensure_ascii=False,indent=2)
    print(json.dumps(dict(selected=report['selected_pair'],checks=checks,
        totals={p:{k:v for k,v in r.items() if k!='levels'} for p,r in report['runs'].items()})))


if __name__=='__main__':main()
