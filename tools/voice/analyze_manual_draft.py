"""Summarize preserved three-turn experiments; never infer acoustic timing from USB."""
import argparse
import hashlib
import json
from pathlib import Path
from fixture_acceptance import check_content


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(directory):
    path = directory / 'report.json'
    report = json.loads(path.read_text(encoding='utf8'))
    rows = []
    for trial in report['trials']:
        events = trial.get('events', [])
        first = lambda stage: next((e for e in events if e['stage'] == stage), None)
        vad, play, draft = first('vad_end'), first('playback'), first('prefetch_prepare')
        stats = first('prefetch_stats')
        stats = json.loads(stats['text']) if stats else None
        rows.append(dict(round=trial['round'], language=trial['language'],
            attempts=len(trial['attempts']), first_wake=bool(trial['attempts'][0]['triggered']),
            complete=trial['complete'], input_complete=trial['input_complete'],
            content_check=trial.get('content_check'),
            content_recheck=(check_content(trial,trial['content_check']['fixture'])
                             if trial.get('content_check') else None),
            task_complete=trial['core_task_complete'], transcript=trial.get('asr', ''),
            draft_input=draft.get('text') if draft else None, stats=stats,
            draft_candidate=(first('prefetch_candidate') or {}).get('text'),
            draft_completed_at=(first('prefetch_response') or {}).get('time_ms'),
            final_asr_received_at=(first('prefetch_input') or {}).get('time_ms'),
            draft_released_at=(first('prefetch_play') or {}).get('time_ms'),
            draft_pcm_before_local_vad_ms=(vad['time_ms']-stats['first_pcm_ms']
                if vad and stats and stats.get('first_pcm_ms') else None),
            playback_after_local_vad_ms=(play['time_ms']-vad['time_ms'] if play and vad else None),
            legacy_vad_end_semantics='Capture joined and upload drained; not microphone stop.',
            playback_after_upload_join_ms=(play['time_ms']-vad['time_ms'] if play and vad else None),
            prepares=sum(e['stage']=='prefetch_prepare' for e in events),
            recoveries=sum(e['stage']=='prefetch_recover' for e in events),
            rejections=[e.get('text') for e in events if e['stage']=='realtime_rejected'],
            errors=[e.get('text') for e in events if e['stage']=='error'],
            emitted_tools=[e.get('text') for e in events if e['stage']=='tool']))
    after, wake = report.get('after', {}), report.get('wake_after', {})
    tail = None
    if report.get('trials'):
        final_events = report['trials'][-1].get('events', [])
        joined = next((e['time_ms'] for e in final_events if e['stage']=='capture_joined'), None)
        began = next((e['time_ms'] for e in final_events if e['stage']=='capture_begin'), None)
        stopped, closed = wake.get('end_at'), wake.get('cue_end_at')
        # This snapshot covers only the final turn; never project it backwards
        # onto earlier turns or infer acoustic onset from the firmware clock.
        if all(v is not None for v in (began,stopped,closed,joined)) and began<=stopped<=closed<=joined:
            tail = dict(round=report['trials'][-1]['round'], capture_stopped_at=stopped,
                        recording_cleanup_done_at=closed, upload_joined_at=joined,
                        cleanup_after_capture_ms=closed-stopped,
                        upload_tail_after_cleanup_ms=joined-closed,
                        semantics='Board clock; cleanup includes final clip commit and speaker close.')
    acoustic_path = directory/'offline-analysis.json'
    return dict(directory=str(directory), report_sha256=digest(path), version=after.get('version'),
        complete=report['complete'], rows=rows, min_heap=after.get('min_heap'),
        worker_stack=after.get('worker_stack'), main_stack=after.get('stack_watermark'),
        last_capture_tail=tail,
        dma_lost=wake.get('dma_lost'), external_recording=report.get('external_recording'),
        acoustic_analysis=(dict(path=str(acoustic_path),sha256=digest(acoustic_path))
                           if acoustic_path.exists() else None))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directories', type=Path, nargs='+')
    p.add_argument('--out', type=Path, required=True)
    args=p.parse_args()
    if args.out.exists():
        raise ValueError('Refuse to replace an evidence report')
    report=dict(accepted=False, acoustic_one_second_passed=False,
        note='Physical rows retained separately by version; internal clocks are not acoustic latency.',
        script_sha256=digest(Path(__file__)), groups=[summarize(d) for d in args.directories])
    report['content_checker_sha256']=digest(Path(__file__).with_name('fixture_acceptance.py'))
    args.out.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps([dict(directory=g['directory'], complete=g['complete'], min_heap=g['min_heap'],
        hit_count=sum(bool(r['stats'] and r['stats']['hit']) for r in g['rows']))
        for g in report['groups']],ensure_ascii=False))


if __name__=='__main__':
    main()
