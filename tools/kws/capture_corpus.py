"""Finite source-separated physical replay corpus; no model/threshold changes."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time
import wave
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/kws'),str(ROOT/'tools')]
from acoustic_test import playback_source,save
from wave_play import play


def balanced(rows):
    groups={}
    for row in sorted(rows,key=lambda r:r['clip_id']):
        groups.setdefault((row['language'],row['source_group']),[]).append(row)
    result=[]
    while any(groups.values()):
        for key in sorted(groups,key=lambda k:(k[1],k[0])):
            if groups[key]:result.append(groups[key].pop(0))
    return result


def validate_selection(chosen, diagnostic=False, comparison=False):
    if diagnostic and comparison:raise ValueError('Choose one capture purpose')
    limit = 12 if comparison else 10 if diagnostic else 64
    split = 'validation' if comparison else 'test' if diagnostic else 'train'
    if not 1 <= len(chosen) <= limit or len({r['clip_id'] for r in chosen}) != len(chosen):
        raise ValueError('Invalid finite capture selection')
    if any(r['split'] != split or not 0 < r['playback_gain'] <= .35 for r in chosen):
        raise ValueError('Capture source split or gain outside the declared purpose')


def selection(mandarin_negatives=False):
    rows=[json.loads(x) for x in (ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl').read_text(encoding='utf8').splitlines()]
    eligible=[]
    for row in rows:
        if row['split'] not in ('train','validation'):continue
        if not row.get('source_group','').startswith('kokoro-'):continue
        with wave.open(str(ROOT/row['path']),'rb') as wav:seconds=wav.getnframes()/wav.getframerate()
        if seconds<=3.5:eligible.append(dict(row,actual_seconds=seconds))
    selected=[]
    for split,positive_count,negative_count in [('train',12,4),('validation',8,2)]:
        for language in (() if mandarin_negatives else ('zh','yue')):
            candidates=balanced([r for r in eligible if r['split']==split and r['label'] and r['language']==language])
            assert len(candidates)>=positive_count
            selected.extend(candidates[:positive_count])
        buckets={kind:[] for kind in ('partial','old','near','ordinary')}
        for row in eligible:
            if row['split']!=split or row['label']:continue
            if mandarin_negatives and row['language']!='zh':continue
            text=row.get('text','')
            kind='partial' if text in ('你好','小言') else 'old' if '乐鑫' in text else 'near' if text.startswith('你好') else 'ordinary'
            buckets[kind].append(row)
        for candidates in buckets.values():
            count=negative_count//2 if mandarin_negatives else negative_count
            ordered=balanced(candidates);assert len(ordered)>=count
            selected.extend(ordered[:count])
    expected=12 if mandarin_negatives else 64
    assert len(selected)==expected and len({r['clip_id'] for r in selected})==expected
    # Fixed tiers, independent of recognition outcomes. Interleave levels within voices.
    counts={}
    for row in selected:
        key=(row['split'],row['language'],row['label'],row['source_group'])
        count=counts.get(key,0);counts[key]=count+1
        row['playback_gain']=.35 if count%2==0 else .175
        row['source_wav_sha256']=hashlib.sha256((ROOT/row['path']).read_bytes()).hexdigest()
    return selected


def main():
    from serial_test import Link
    from calibrate_acoustic import export_clip
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True)
    p.add_argument('--mandarin-negatives',action='store_true')
    p.add_argument('--selection',type=Path)
    p.add_argument('--diagnostic', action='store_true', help='At most ten TEST-source captures for diagnosis only; never training data')
    p.add_argument('--comparison',action='store_true',help='At most twelve VALIDATION captures for same-PCM model comparison, never training')
    p.add_argument('--version',default='0.6.3-context');args=p.parse_args()
    args.out.mkdir(parents=True,exist_ok=False)
    if (args.diagnostic or args.comparison) and not args.selection:
        raise ValueError('Diagnostic capture requires an explicit frozen selection')
    if args.selection:
        assert not args.mandarin_negatives
        chosen=json.loads(args.selection.read_text(encoding='utf8'))
        validate_selection(chosen, args.diagnostic, args.comparison)
        for row in chosen:
            assert hashlib.sha256((ROOT/row['path']).read_bytes()).hexdigest()==row['source_wav_sha256']
    else:chosen=selection(args.mandarin_negatives)
    save(args.out/'selection.json',chosen)
    report=dict(complete=False,started=time.time(),trials=[],max_trials=len(chosen),
                diagnostic_only=args.diagnostic,comparison_only=args.comparison,
                script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    link=Link('COM5',args.out/'serial.log')
    try:
        report['identity']=link.command('agent status');link.off()
        assert report['identity']['version']==args.version
        report['previous_clip']=export_clip(link,args.out/'previous-clip.wav')
        for index,row in enumerate(chosen):
            options=SimpleNamespace(out=args.out,source_rms=.14)
            source=playback_source(row,options,report)
            link.command('agent audio capture 6000');deadline=time.monotonic()+12
            while time.monotonic()<deadline:
                query_started=time.monotonic();ready=link.command('agent audio status');ready_at=time.monotonic()
                if ready['recording'] and ready['capture_stage']==2:break
                time.sleep(.05)
            else:raise TimeoutError('No ADC readiness; no playback')
            time.sleep(.5)
            audio=play(source,None,row['playback_gain'])
            deadline=time.monotonic()+10
            while time.monotonic()<deadline:
                state=link.command('agent audio status')
                if not state['recording'] and state['clip_ready']:break
                time.sleep(.1)
            else:raise TimeoutError('Capture completion')
            assert state['capture_error']=='ok' and state['clip_ms']==6000
            path=args.out/f'{index:03d}-{row["clip_id"]}.wav'
            trial=dict(index=index,source=row,path=path.as_posix(),audio=audio,ready=ready,
                ready_query_started=query_started,ready_query_finished=ready_at,state=state,
                signal=export_clip(link,path),finished=time.time())
            report['trials'].append(trial);save(args.out/'report.json',report)
            print(json.dumps(dict(done=index+1,total=len(chosen),split=row['split'],language=row['language'],
                                  label=row['label'],gain=row['playback_gain'])),flush=True)
        report['complete']=True
    except BaseException as error:
        report['error']=repr(error);raise
    finally:
        link.command('agent audio stop');link.close()
        report['ended']=time.time();save(args.out/'report.json',report)


if __name__=='__main__':main()
