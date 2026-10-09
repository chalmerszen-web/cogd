"""Offline review of bounded allocation/transition observations; no device I/O.

These rings do not enumerate all live allocations. Missing/skipped records are
reported; absence of a KWS marker is not proof of absence of allocation overlap.
"""
import argparse
import hashlib
import json
from pathlib import Path

PHASES={'capture_begin','answer_end','rearm_begin','rearm_end','http_release_begin',
        'http_release_end','kws_alloc_begin','kws_alloc_end','cleanup_end',
        'workspace_restore_begin','workspace_restore_end','voice_off_begin','voice_off_end'}


def ring(events, row_stage, summary_stage, count_key):
    summaries=[json.loads(e['text']) for e in events if e['stage']==summary_stage]
    rows=[json.loads(e['text']) for e in events if e['stage']==row_stage]
    if len(summaries)!=1:
        return dict(available=False,reason='Missing or duplicate summary',rows=rows)
    summary=summaries[0]
    total, overwritten=summary[count_key],summary['overwritten']
    if any(type(n) is not int or not 0<=n<=0xffffffff for n in (total,overwritten,summary['skipped'])):
        raise ValueError('Invalid ring counters')
    if overwritten!=max(0,total-16) or [r['seq'] for r in rows]!=list(range(overwritten+1,total+1)):
        return dict(available=False,reason='Missing/out-of-order/duplicated ring rows',summary=summary,rows=rows)
    for row in rows:
        if any(type(row[k]) is not int or not 0<=row[k]<=0xffffffff for k in ('at_ms','free')):
            raise ValueError('Invalid ring observation')
        if count_key=='points' and row['phase'] not in PHASES: raise ValueError('Unknown phase')
    if any(b['at_ms']<a['at_ms'] for a,b in zip(rows,rows[1:])): raise ValueError('Noncausal ring timestamps')
    if count_key=='minima':
        if 'sdk_minimum' in summary:
            for r in rows:
                if type(r.get('sdk_minimum')) is not int or not 0<=r['sdk_minimum']<=0xffffffff:
                    raise ValueError('Invalid SDK minimum')
            lowest=summary['initial'] if not overwritten else (rows[0]['free'] if rows else summary['minimum'])
            sdk=summary['initial_sdk_minimum'] if not overwritten else (rows[0]['sdk_minimum'] if rows else summary['sdk_minimum'])
            for r in rows[1:] if overwritten else rows:
                if r['sdk_minimum']>sdk or (r['free']>=lowest and r['sdk_minimum']>=sdk):
                    raise ValueError('Neither allocation minimum improved')
                lowest=min(lowest,r['free']);sdk=r['sdk_minimum']
            if sdk!=summary['sdk_minimum'] or (not overwritten and lowest!=summary['minimum']) or summary['minimum']>lowest:
                raise ValueError('Allocation minima contradict summary')
        elif any(b['free']>=a['free'] for a,b in zip(rows,rows[1:])) or (rows and rows[-1]['free']!=summary['minimum']):
            raise ValueError('Allocation minima contradict summary')
    return dict(available=True,summary=summary,rows=rows,all_callbacks_observed=summary['skipped']==0)


def analyze(report):
    result=[]
    for trial in report['trials']:
        events=trial['events']
        allocations=ring(events,'voice_heap','voice_heap_summary','minima')
        points=ring(events,'voice_heap_point','voice_heap_points_summary','points')
        row=dict(round=trial['round'],language=trial['language'],full_input=trial['input_complete'],
                 allocation_minima=allocations,transitions=points,
                 KWS_admitted_before_HTTP_release_return=None,HTTP_release_free_delta=None,
                 SDK_cumulative_min=min(e['min_heap'] for e in events))
        if points['available']:
            found={name:[(i,p) for i,p in enumerate(points['rows']) if p['phase']==name] for name in PHASES}
            begin,end=found['http_release_begin'],found['http_release_end']
            if len(begin)==len(end)==1 and begin[0][0]<end[0][0]:
                row['HTTP_release_free_delta']=end[0][1]['free']-begin[0][1]['free']
                row['HTTP_release_begin_ms']=begin[0][1]['at_ms']
                row['HTTP_release_end_ms']=end[0][1]['at_ms']
                admitted=found['kws_alloc_end']
                if len(admitted)==1:
                    row['KWS_admitted_before_HTTP_release_return']=admitted[0][0]<end[0][0]
            answer=found['answer_end']
            if len(answer)==1 and allocations['available']:
                row['minima_after_final_HTTP_producer_return']=[p for p in allocations['rows']
                    if p['at_ms']>=answer[0][1]['at_ms'] and (not begin or p['at_ms']<begin[0][1]['at_ms'])]
        result.append(row)
    return dict(rounds=result,latency_acceptance_eligible=False,
        limitations=['Successful-allocation callbacks sample simultaneous total free bytes, but skipped hooks/idle intervals are not reconstructed.',
            'SDK cumulative minimum sums per-region historical minima; those times need not coincide.',
            'Free-byte delta around release may include other tasks; it is not an exact object size.',
            'No KWS marker in a job is inconclusive: model admission may occur after the observer stops.',
            'Diagnostic scheduling and per-turn export exclude production speed and rapid-rearm acceptance.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    source=args.directory/'report.json';output=args.directory/'voice-lifetime-analysis.json'
    if output.exists(): raise FileExistsError(output)
    raw=source.read_bytes();result=analyze(json.loads(raw))
    result.update(report_sha256=hashlib.sha256(raw).hexdigest(),
                  script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(dict(rounds=[{k:v for k,v in r.items() if k not in
        ('allocation_minima','transitions','minima_after_final_HTTP_producer_return')} for r in result['rounds']])),flush=True)
    if not all(r['allocation_minima']['available'] and r['transitions']['available'] for r in result['rounds']):
        raise SystemExit(1)


if __name__=='__main__': main()
