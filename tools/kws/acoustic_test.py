"""Bounded learned-model speaker replay and ambient observation on the full Agent.

Polling latency is an upper-bound estimate, not a calibrated acoustic timestamp.
An ambient trigger is not automatically a false alarm: the room is uncontrolled.
"""
import argparse
import array
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import json
import hashlib
import math
from pathlib import Path
import sys
import time
import wave

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))


def save(path,value):
    temporary=path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value,indent=2,ensure_ascii=False)+'\n',encoding='utf8')
    temporary.replace(path)


def playback_source(row,args,report):
    path=ROOT/row['path']
    if not args.source_rms:return path
    directory=args.out/'leveled-sources';directory.mkdir(exist_ok=True)
    target=directory/(row['clip_id']+'.wav')
    if target.exists():return target
    with wave.open(str(path),'rb') as wav:
        if (wav.getnchannels(),wav.getsampwidth(),wav.getframerate())!=(1,2,16000):
            raise ValueError('Source leveling requires mono PCM16 16 kHz')
        raw=wav.readframes(wav.getnframes());pcm=array.array('h',raw)
    if not pcm:raise ValueError('Empty replay source')
    rms=math.sqrt(sum(x*x for x in pcm)/len(pcm))/32768;peak=max(abs(x) for x in pcm)/32768
    gain=min(args.source_rms/max(rms,1e-9),.8/max(peak,1e-9))
    leveled=array.array('h',(round(x*gain) for x in pcm))
    with wave.open(str(target),'wb') as wav:
        wav.setparams((1,2,16000,0,'NONE','not compressed'));wav.writeframes(leveled.tobytes())
    report.setdefault('source_levels',[]).append(dict(clip_id=row['clip_id'],source_pcm_sha256=hashlib.sha256(raw).hexdigest(),
        source_rms=rms,source_gain=gain,target_rms=args.source_rms,peak_cap=.8,
        leveled_pcm_sha256=hashlib.sha256(leveled.tobytes()).hexdigest()))
    return target


def wait_listening(link,timeout=15):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        state=link.command('agent wake status')
        if state['state']=='listening':return state
        if state['state']=='failed':raise RuntimeError('Wake listener failed: '+str(state))
        time.sleep(.1)
    raise TimeoutError('Wake listener did not resume')


def select_negatives(rows,count):
    """Cover partial, old, confusable and ordinary speech before extra examples."""
    buckets={k:[] for k in ('partial','old','near','ordinary')}
    for row in sorted(rows,key=lambda r:r['clip_id']):
        text=row.get('text','').replace('，','').replace(' ','')
        kind='partial' if text in ('你好','小言') else 'old' if any(x in text for x in ('乐鑫','樂鑫')) else 'near' if text.startswith('你好') else 'ordinary'
        buckets[kind].append(row)
    def balanced(candidates):
        groups={}
        for row in candidates:groups.setdefault(row['language'],[]).append(row)
        result=[]
        while any(groups.values()):
            for key in sorted(groups):
                if groups[key]:result.append(groups[key].pop(0))
        return result
    selected=[]
    for kind,quota in [('partial',7),('old',2),('near',6),('ordinary',5)]:
        if kind=='old':buckets[kind].sort(key=lambda r:r.get('text')!='嗨乐鑫')
        selected.extend(balanced(buckets[kind])[:quota])
    if len(selected)<count:raise ValueError('Insufficient negative category coverage')
    return selected[:count]


def observation_complete(audio,row,now):
    end=audio['source_started']+(row.get('wake_end_sample') or 0)/16000
    # Negative manifests have no keyword endpoint. Always retain a tail after
    # actual playback completion so delayed false triggers remain observable.
    return now>max(audio['finished'],end)+1.2


def resource_summary(samples,profile):
    blocks=profile['blocks'];histogram=profile['histogram'];cumulative=0;p99=None
    if sum(histogram)!=blocks:raise ValueError('Incomplete profile histogram')
    for index,count in enumerate(histogram):
        cumulative+=count
        if p99 is None and blocks and cumulative*100>=99*blocks:p99=(index+1)*profile['bin_us']
    failures=[]
    if not blocks:failures.append('no inferred audio')
    if profile['max_us']>=32000 or (p99 is not None and p99>16000):failures.append('inference deadline')
    if any(s['status']['min_heap']<32768 for s in samples):failures.append('minimum internal heap')
    listening=[s for s in samples if s['wake']['state']=='listening']
    if not listening:failures.append('no listening samples')
    elif min(s['status']['largest_block'] for s in listening)<24576:failures.append('largest listening block')
    # Frames actually consumed by the KWS backend, excluding recording/cooldown.
    return dict(blocks=blocks,inferred_audio_seconds=blocks*.032,p99_us_upper_bound=p99,
                max_us=profile['max_us'],failures=failures)


def replay(link,args,rows,report):
    from wave_play import play
    selected=[]
    for language in ('zh','yue'):
        candidates=[r for r in rows if r['split']==args.split and r['label']==1 and r['language']==language]
        # Deterministic interleaving distributes the finite subset across voices.
        groups={}
        for row in sorted(candidates,key=lambda r:r['clip_id']):groups.setdefault(row['source_group'],[]).append(row)
        ordered=[]
        while any(groups.values()):
            for group in sorted(groups):
                if groups[group]:ordered.append(groups[group].pop(0))
        if len(ordered)<args.per_language:raise ValueError('Too few reserved replay samples')
        selected.extend(ordered[:args.per_language])
    if args.negatives:
        candidates=[r for r in rows if r['split']==args.split and not r['label']]
        selected.extend(select_negatives(candidates,args.negatives))
    report['source_files']=[dict(clip_id=r['clip_id'],wav_sha256=hashlib.sha256((ROOT/r['path']).read_bytes()).hexdigest()) for r in selected]
    save(args.out/'selection.json',selected)
    report['trials']=[];report['samples']=[]
    link.off();link.command('agent kws reset-profile')
    with ThreadPoolExecutor(max_workers=1) as executor:
        for row in selected:
            link.command('agent cancel');link.command('agent wake on');wait_listening(link)
            # Full detector warm-up after any backend re-creation.
            time.sleep(2.2);before=link.command('agent wake status')
            if before['state']!='listening':raise RuntimeError('Not listening after warm-up; trial not started')
            report['samples'].append(dict(wake=before,status=link.command('agent status')))
            noise_meter=link.command('agent audio status') if args.meter else None
            meters=[]
            source=playback_source(row,args,report)
            started=time.monotonic();future=executor.submit(play,source,args.device,args.gain)
            deadline=started+row['seconds']+4;first=None;last=before
            while time.monotonic()<deadline:
                last=link.command('agent wake status')
                if last['wakes']<before['wakes']:raise RuntimeError('Device counter reset')
                if last['dma_lost']!=before['dma_lost']:raise RuntimeError('DMA loss during playback')
                if last['wakes']>before['wakes'] and first is None:first=time.monotonic()
                if args.meter:
                    value=link.command('agent audio status')
                    meters.append(dict(at=time.monotonic(),wake_state=last['state'],
                        rms=value['mic_rms'],peak=value['mic_peak'],valid=value['mic_valid'],age_ms=value['mic_age_ms']))
                if future.done() and observation_complete(future.result(),row,time.monotonic()):break
                time.sleep(.05)
            audio=future.result(timeout=5)
            observed_until=time.monotonic()
            low=audio['source_started']+(row.get('target_end_low_sample') or row.get('wake_end_sample') or 0)/16000
            word_end=audio['source_started']+(row.get('target_end_high_sample') or row.get('wake_end_sample') or 0)/16000
            trial=dict(clip_id=row['clip_id'],language=row['language'],source_group=row['source_group'],
                label=row['label'],text=row.get('text'),
                gain=args.gain,output=audio,before=before,after=last,triggered=first is not None,
                valid_hit=bool(row['label'] and first is not None and low<=first<=word_end+.8),
                early=bool(row['label'] and first is not None and first<low),
                observed_after_playback_ms=(observed_until-audio['finished'])*1000,
                noise_meter=noise_meter,meters=meters,
                observed_latency_ms=(first-word_end)*1000 if first is not None else None,
                latency_note='Host polling and uncalibrated audio output timing; not device-only latency')
            report['trials'].append(trial);save(args.out/'replay.json',report)
            print(json.dumps(dict(clip_id=row['clip_id'],triggered=trial['triggered']),ensure_ascii=False),flush=True)
            link.command('agent cancel')
    report['languages']={language:dict(total=sum(t['language']==language and t['label']==1 for t in report['trials']),
        triggers=sum(t['language']==language and t['label']==1 and t['triggered'] for t in report['trials']),
        valid_hits=sum(t['language']==language and t['valid_hit'] for t in report['trials'])) for language in ('zh','yue')}
    negative=[t for t in report['trials'] if not t['label']]
    report['negatives']=dict(total=len(negative),triggers=sum(t['triggered'] for t in negative))
    link.off();report['profile']=link.command('agent kws profile')
    report['resources']=resource_summary(report['samples'],report['profile'])


def observe(link,args,rows,report):
    from wave_play import play
    negatives=[r for r in rows if r['split']=='test' and not r['label']]
    if not negatives:raise ValueError('No reserved interference sources')
    report.update(samples=[],events=[],playbacks=[],environment='No live confirmation of room silence; uncontrolled ambient sound may be present')
    link.off();link.command('agent kws reset-profile');link.command('agent wake on');wait_listening(link)
    before=link.command('agent wake status');report['wake_before']=before
    start=time.monotonic();previous=before;next_sample=0;index=0;future=None;current=None
    with ThreadPoolExecutor(max_workers=1) as executor:
        while time.monotonic()-start<args.seconds:
            elapsed=time.monotonic()-start;phase='ambient' if elapsed<args.seconds/2 else 'controlled-interference'
            if future is not None and future.done():
                report['playbacks'].append(dict(clip_id=current['clip_id'],output=future.result()));future=None
            if phase=='controlled-interference' and future is None:
                current=negatives[index%len(negatives)];index+=1
                if current['seconds']+.5<args.seconds-elapsed:
                    future=executor.submit(play,playback_source(current,args,report),args.device,args.gain)
            wake=link.command('agent wake status')
            if wake['wakes']<previous['wakes']:raise RuntimeError('Device counter reset')
            if wake['wakes']>previous['wakes']:
                report['events'].append(dict(elapsed=elapsed,phase=phase,count=wake['wakes']-previous['wakes'],
                    source_clip=current['clip_id'] if future else None,wake=wake))
                # Stop the bounded post-wake recorder so observation resumes.
                link.command('agent cancel')
                link.command('agent wake on');wait_listening(link)
            if wake['state']=='failed':raise RuntimeError('Listener entered failed state')
            if elapsed>=next_sample:
                sample=dict(elapsed=elapsed,phase=phase,wake=wake,status=link.command('agent status'),profile=link.command('agent kws profile'))
                report['samples'].append(sample);next_sample=elapsed+5
                save(args.out/'observation.json',report)
            previous=wake;time.sleep(.2)
        if future is not None:report['playbacks'].append(dict(clip_id=current['clip_id'],output=future.result(timeout=95)))
    link.off();profile=link.command('agent kws profile');after=link.command('agent wake status')
    report.update(profile=profile,wake_after=after,wall_seconds=time.monotonic()-start,
                  resources=resource_summary(report['samples'],profile))
    if after['dma_lost']!=before['dma_lost']:report['resources']['failures'].append('DMA loss')
    counts=Counter()
    for event in report['events']:counts[event['phase']]+=event['count']
    report['event_counts']=dict(counts)
    report['interpretation']='Observed triggers and actual inferred audio duration only; not a production false-alarm rate or proof of a quiet room.'


def main():
    from serial_test import Link
    ap=argparse.ArgumentParser();ap.add_argument('mode',choices=['replay','observe'])
    ap.add_argument('--manifest',type=Path,required=True);ap.add_argument('--threshold',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True);ap.add_argument('--port',default='COM5')
    ap.add_argument('--seconds',type=int,default=1800);ap.add_argument('--per-language',type=int,default=10)
    ap.add_argument('--gain',type=float,default=.35)
    ap.add_argument('--device',type=int,default=None)
    ap.add_argument('--split',choices=['test','validation'],default='test')
    ap.add_argument('--negatives',type=int,default=0)
    ap.add_argument('--meter',action='store_true',help='Record received level while replaying; no acoustic-distance claim')
    ap.add_argument('--source-rms',type=float,default=0,help='Optional bounded playback source leveling; captured sound is unchanged')
    args=ap.parse_args()
    if not 10<=args.seconds<=1800 or not 0<=args.per_language<=20 or not 0<=args.negatives<=20 or not (args.per_language or args.negatives or args.mode=='observe') or not 0<args.gain<=.5 or not 0<=args.source_rms<=.14:
        raise ValueError('Outside bounded test limits')
    args.out.mkdir(parents=True,exist_ok=False)
    rows=[json.loads(line) for line in args.manifest.read_text(encoding='utf8').splitlines()]
    threshold=json.loads(args.threshold.read_text())['device_threshold_per_mille']
    link=Link(args.port,args.out/'serial.log');before=None;report=dict(complete=False,started=time.time(),mode=args.mode,
        settings=dict(threshold=threshold,device=args.device,gain=args.gain,source_rms=args.source_rms,
                      split=args.split,per_language=args.per_language,negatives=args.negatives,meter=args.meter),
        script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        manifest_sha256=hashlib.sha256(args.manifest.read_bytes()).hexdigest())
    try:
        report['identity']=identity=link.command('agent status')
        if identity['version']!='0.7.0-xiaoyan-exp':raise RuntimeError('Expected trained experimental firmware')
        if not link.command('agent kws profile')['trained']:raise RuntimeError('Untrained probe is not an acoustic model')
        before=link.command('agent wake status');link.command('agent wake threshold '+str(threshold))
        (replay if args.mode=='replay' else observe)(link,args,rows,report)
        report['complete']=True
    except BaseException as error:
        report['error']=repr(error);raise
    finally:
        try:
            if before is not None:
                link.command('agent cancel');link.command('agent wake on' if before['enabled'] else 'agent wake off')
        finally:
            link.close();report['ended']=time.time();save(args.out/(args.mode+'.json'),report)

if __name__=='__main__':main()
