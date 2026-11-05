"""Finite acoustic replay of a frozen manifest; no synthetic hit claims.

The host speaker plays WAVs and the board microphone/KWS must produce events.
Rates describe these samples and this placement, not unseen human recall.
"""
import argparse,concurrent.futures,hashlib,json,re,shutil,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/voice'),str(ROOT/'tools')]
from device import Device
from wave_play import play
from conversation_test import leveled

p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
p.add_argument('--port',default='COM3');p.add_argument('--gain',type=float,default=.35);p.add_argument('--warmup',type=float,default=3.3)
p.add_argument('--firmware',help='Require this device-reported version before replay')
p.add_argument('--app',type=Path,default=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/build/cogd_p4.bin',help='Exact candidate binary whose hash remains fixed throughout replay')
p.add_argument('--stop-if-acceptance-impossible',action='store_true',
               help='Development only: stop on a negative trigger or when any positive group cannot reach 90 percent')
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
rows=[json.loads(s) for s in a.manifest.read_text(encoding='utf8').splitlines() if s.strip()]
assert 0<len(rows)<=400 and 0<a.gain<=.5
report={'complete':False,'manifest_sha256':hashlib.sha256(a.manifest.read_bytes()).hexdigest(),'trials':[],
        'started':time.time(),'gain':None if any('playback_gain' in row for row in rows) else a.gain,
        'default_gain':a.gain,'playback_gains':sorted({float(row.get('playback_gain',a.gain)) for row in rows}),
        'gain_source':'manifest overrides CLI default per row','warmup_seconds':a.warmup,
        'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'scope':'Fixed host-speaker replay through board microphone; no human or distance-generalization claim'}
candidate=a.app.resolve()
report['candidate_app_sha256']=hashlib.sha256(candidate.read_bytes()).hexdigest()
report['development_early_stop']=a.stop_if_acceptance_impossible
d=Device(a.out,a.port)
def save():
    (a.out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');d.save()
def drain(seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        for line in d.lines():
            if re.search(r'Guru Meditation|assert failed|ESP-ROM:',line):raise RuntimeError(line)
def listening():
    end=time.monotonic()+12
    while time.monotonic()<end:
        v=d.command('agent wake status',query=True,timeout=4)
        if v['state']=='listening':return v
        time.sleep(.1)
    raise TimeoutError('Listener did not become ready')
try:
    report['before']={'system':d.command('agent status',query=True,timeout=8),'wake':d.command('agent wake status',query=True,timeout=8)}
    assert report['before']['system']['device_id']=='esp-hi-e8f60aeb7a3f'
    if a.firmware:assert report['before']['system']['version']==a.firmware
    d.command('agent voice off',timeout=12)
    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
        for row in rows:
            source=ROOT/row['path'];assert hashlib.sha256(source.read_bytes()).hexdigest()==row['wav_sha256']
            d.command('agent cancel',timeout=8);d.command('agent wake on',timeout=8);listening();drain(a.warmup)
            before=d.command('agent wake status',query=True,timeout=4)
            profile_before=d.command('agent kws profile',query=True,timeout=4)
            target=a.out/(row['clip_id']+'.wav')
            if row.get('prepared_level'):
                shutil.copy2(source,target);level={'prepared_level':True,'normalization_applied':False,'source_sha256':row['wav_sha256']}
            else:level=leveled(source,target)
            playback_gain=float(row.get('playback_gain',a.gain));assert 0<playback_gain<=.5
            trial={'source':row,'before':before,'profile_before':profile_before,'levels':level,'meter':[],'hit':False}
            trial['playback_gain']=playback_gain
            future=pool.submit(play,target,gain=playback_gain);deadline=time.monotonic()+20
            while time.monotonic()<deadline:
                state=d.command('agent wake status',query=True,timeout=4)
                assert state['wakes']>=before['wakes'] and state['dma_lost']==before['dma_lost'],'Device reset or dropped PCM'
                if state['wakes']>before['wakes'] and not trial['hit']:trial.update(hit=True,observed_at=time.monotonic())
                meter=d.command('agent audio status',query=True,timeout=4)
                trial['meter'].append({k:meter.get(k) for k in ['mic_rms','mic_peak','mic_bias','mic_age_ms']})
                if future.done() and time.monotonic()>future.result()['finished']+1:break
                time.sleep(.08)
            trial['playback']=future.result(timeout=4);trial['after']=state
            trial['profile_after']=d.command('agent kws profile',query=True,timeout=4)
            report['trials'].append(trial);save()
            print(json.dumps({'n':len(report['trials']),'group':row.get('wake_group',row['language']),'label':row['label'],'hit':trial['hit'],'text':row['text']},ensure_ascii=False),flush=True)
            d.command('agent cancel',timeout=8)
            if a.stop_if_acceptance_impossible:
                if not row['label'] and trial['hit']:raise RuntimeError('Development negative triggered; acceptance impossible')
                group=row.get('wake_group',row['language'])
                total=sum(bool(r['label']) and r.get('wake_group',r['language'])==group for r in rows)
                misses=sum(bool(t['source']['label']) and t['source'].get('wake_group',t['source']['language'])==group and not t['hit'] for t in report['trials'])
                if total and 10*(total-misses)<9*total:raise RuntimeError('Development recall cannot reach 90 percent: '+group)
    groups={}
    for row in report['trials']:
        s=row['source'];key=s.get('wake_group',s['language']) if s['label'] else 'negative'
        g=groups.setdefault(key,{'trials':0,'triggers':0});g['trials']+=1;g['triggers']+=int(row['hit'])
    assert hashlib.sha256(candidate.read_bytes()).hexdigest()==report['candidate_app_sha256'],'Build changed during physical replay'
    conditions={}
    for trial in report['trials']:
        s=trial['source'];name=s.get('condition','default');key=s.get('wake_group',s['language']) if s['label'] else 'negative'
        g=conditions.setdefault(name,{}).setdefault(key,{'trials':0,'triggers':0});g['trials']+=1;g['triggers']+=int(trial['hit'])
    report.update(complete=True,groups=groups,conditions=conditions)
except Exception as e:report['error']=str(e);raise
finally:
    try:d.command('agent cancel',timeout=8);d.command('agent voice on',timeout=8)
    finally:report['ended']=time.time();save();d.close()
