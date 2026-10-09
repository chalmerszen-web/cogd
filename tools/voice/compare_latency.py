"""Compare phase timings for matched synthetic acoustic conversations."""
import argparse
import json
from pathlib import Path
import statistics


def timings(path):
    report=json.loads((path/'report.json').read_text(encoding='utf8'))
    rows=[]
    for trial in report['trials']:
        if not trial.get('complete'):continue
        event={e['stage']:e for e in trial['events']}
        def elapsed(a,b):return round((event[b]['time_ms']-event[a]['time_ms'])/1000,3)
        row={'id':trial['id'],'wake_language':trial['wake_language'],
             'utterance_sha256':trial['utterance_source']['source_sha256'],
             'wake_sha256':trial['wake_source']['source_sha256'],
             'first_pcm_s':elapsed('vad_end','playback'),'complete_s':elapsed('vad_end','done'),
             'upload_s':elapsed('upload','asr_submit'),'submit_s':elapsed('asr_submit','asr_wait'),
             'asr_wait_download_s':elapsed('asr_wait','asr_text'),
             'llm_s':elapsed('llm','tts'),'tts_s':elapsed('tts','playback'),
             'input_end_to_reply_s':round(event['playback']['observed']-trial['utterance_playback']['finished'],3),
             'free_heap':trial['status']['free_heap'],'min_heap':trial['status']['min_heap'],
             'worker_stack':trial['status']['worker_stack'],'rearmed':trial['wake']['state']=='listening'}
        for name,start,end in [('policy_s','upload','upload_send'),('cloud_wait_s','asr_wait','asr_ready'),
                               ('result_download_s','asr_ready','asr_text')]:
            if start in event and end in event:row[name]=elapsed(start,end)
        rows.append(row)
    return rows


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True);p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    before=timings(a.baseline);after=timings(a.candidate)
    assert [(r['id'],r['wake_language']) for r in before]==[(r['id'],r['wake_language']) for r in after], 'Matched inputs required'
    assert [(r['utterance_sha256'],r['wake_sha256']) for r in before]==[(r['utterance_sha256'],r['wake_sha256']) for r in after], 'Identical source audio required'
    summary={'baseline':str(a.baseline),'candidate':str(a.candidate),'matched_pairs':len(before),'before':before,'after':after}
    summary['medians']={key:{'before':statistics.median(r[key] for r in before),
                             'after':statistics.median(r[key] for r in after)}
                        for key in before[0] if key.endswith('_s')}
    m=summary['medians']['first_pcm_s'];summary['first_pcm_reduction_percent']=round(100*(1-m['after']/m['before']),1)
    a.out.parent.mkdir(parents=True,exist_ok=True)
    a.out.write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({'pairs':len(before),'medians':summary['medians'],'reduction_percent':summary['first_pcm_reduction_percent']},ensure_ascii=False))


if __name__=='__main__':main()
