"""Four positives/language and six negatives, one frozen same-board comparison."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/kws'),str(ROOT/'tools/voice'),str(ROOT/'tools')]
from acoustic_test import replay,save


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_rows(rows):
    if len(rows)!=14 or len({r['clip_id'] for r in rows})!=14:
        raise ValueError('Expected exactly 14 distinct fixed clips')
    for lang in ('zh','yue'):
        if sum(r['label']==1 and r['language']==lang for r in rows)!=4:
            raise ValueError('Expected four positive clips per language')
    if sum(not r['label'] for r in rows)!=6:raise ValueError('Expected six negatives')
    seen=set()
    for row in rows:
        if row['split']!='validation' or row['language'] not in ('zh','yue') or not 0<row['seconds']<=6:
            raise ValueError('Only bounded development sources are allowed')
        path=(ROOT/row['path']).resolve()
        if not path.is_relative_to(ROOT):raise ValueError('Source outside workspace')
        expected=row.get('wav_sha256',row.get('source_wav_sha256'))
        if not expected or digest(path)!=expected:raise ValueError('Changed replay WAV: '+row['clip_id'])
        if expected in seen:raise ValueError('Duplicate PCM source disguised as a distinct clip')
        seen.add(expected)


class ReplayLink:
    """Use the voice-aware USB parser so cancelled workers cannot desynchronize queries."""
    def __init__(self,device):self.device=device
    def command(self,text,timeout=30):
        query=text in ('agent status','agent wake status','agent voice status','agent audio status',
                       'agent context stats','agent kws profile') or text.startswith('agent audio clip read ')
        return self.device.command(text,query=query,timeout=timeout)
    def off(self):
        self.command('agent wake off')
        deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            value=self.command('agent wake status')
            if not value['enabled'] and value['chunk']==0:
                audio=self.command('agent audio status')
                status=self.command('agent status')
                if not audio['recording'] and not audio['playing'] and not status['busy']:return value
            time.sleep(.1)
        raise TimeoutError('Wake engine did not release')


def capture_settings(wake,voice):
    """Validate the real runtime contract before changing any device settings."""
    saved=dict(threshold=wake['threshold'],input_gain=wake['input_gain'],
        wake_enabled=wake['enabled'],voice_enabled=voice['voice_enabled'])
    if not 500<=saved['threshold']<=950 or not 1<=saved['input_gain']<=4:
        raise ValueError('Unsupported saved wake settings')
    if not isinstance(saved['wake_enabled'],bool) or not isinstance(saved['voice_enabled'],bool):
        raise ValueError('Invalid saved enable state')
    return saved


def restore_settings(link,saved):
    link.command('agent cancel');link.off()
    link.command('agent wake threshold '+str(saved['threshold']))
    link.command('agent wake gain '+str(saved['input_gain']))
    if saved['voice_enabled']:link.command('agent voice on')
    elif saved['wake_enabled']:link.command('agent wake on')
    return link.command('agent wake status')


def compare(before,after):
    for report in (before,after):
        if not report.get('complete') or len(report['trials'])!=14:raise ValueError('Incomplete replay')
    if (before['pair'],after['pair'])!=('ek','el'):raise ValueError('Expected E/K before E/L')
    for key in ('manifest_sha256','script_sha256','settings','source_files'):
        if before[key]!=after[key]:raise ValueError('Unmatched '+key)
    if before['identity']['device_id']!=after['identity']['device_id']:raise ValueError('Different device')
    if before['output_name']!=after['output_name']:raise ValueError('Different speaker endpoint')
    b={r['clip_id']:r for r in before['trials']};a={r['clip_id']:r for r in after['trials']}
    if set(a)!=set(b):raise ValueError('Different evaluated clips')
    return dict(complete=True,matched_trials=14,
        before={k:before[k] for k in ('languages','negatives','resources')},
        after={k:after[k] for k in ('languages','negatives','resources')},
        changed=[dict(clip_id=k,label=a[k]['label'],language=a[k]['language'],text=a[k]['text'],
            before_triggered=b[k]['triggered'],after_triggered=a[k]['triggered'],
            before_valid=b[k]['valid_hit'],after_valid=a[k]['valid_hit']) for k in a
            if (a[k]['triggered'],a[k]['valid_hit'])!=(b[k]['triggered'],b[k]['valid_hit'])],
        limitation='Same sources, board, endpoint and digital gain. Sequential room/cloud state is not randomized. Synthetic replay, not human bilingual generalization; polling and energy endpoints are approximate.')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pair',choices=('ek','el'))
    p.add_argument('--manifest',type=Path,default=ROOT/'training/kws/releases/compact-l/replay-selection.jsonl')
    p.add_argument('--out',type=Path)
    p.add_argument('--port',default='COM5')
    p.add_argument('--gain',type=float,default=.35)
    p.add_argument('--source-rms',type=float,default=.14)
    p.add_argument('--check',action='store_true',help='Validate inputs only; do not open USB or any audio device')
    p.add_argument('--compare',nargs=2,type=Path,metavar=('BEFORE','AFTER'))
    a=p.parse_args()
    if a.compare:
        if not a.out or a.out.exists():raise ValueError('Choose a new comparison JSON output')
        result=compare(*(json.loads(path.read_text(encoding='utf8')) for path in a.compare))
        a.out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
        print(json.dumps(result,ensure_ascii=False));return
    if not 0<a.gain<=.5 or not 0<a.source_rms<=.14:raise ValueError('Outside fixed level limits')
    rows=[json.loads(line) for line in a.manifest.read_text(encoding='utf8').splitlines()]
    validate_rows(rows)
    if a.check:
        print(json.dumps(dict(validated=True,clips=len(rows),manifest_sha256=digest(a.manifest),hardware_opened=False)));return
    if not a.pair or not a.out:raise ValueError('Live replay requires pair and new output directory')
    a.out.mkdir(parents=True,exist_ok=False)
    from device import Device
    from wave_play import output_devices
    from calibrate_acoustic import export_clip
    outputs=[(i,n) for i,n in output_devices() if 'Misiom-Shooter' in n]
    if len(outputs)!=1:raise ValueError('Expected one named speaker endpoint')
    a.device=outputs[0][0];a.split='validation';a.per_language=4;a.negatives=6;a.meter=True
    d=Device(a.out,a.port);link=ReplayLink(d);previous=None;voice=None;touched=False
    report=dict(complete=False,pair=a.pair,started=time.time(),mode='fixed_bilingual_comparison',
        settings=dict(threshold=740,wake_gain=1,gain=a.gain,source_rms=a.source_rms,
            split=a.split,per_language=4,negatives=6,meter=True),
        output_name=outputs[0][1],manifest_sha256=digest(a.manifest),script_sha256=digest(Path(__file__)))
    try:
        report['identity']=link.command('agent status')
        previous=link.command('agent wake status');voice=link.command('agent voice status')
        if previous['model']!='xiaoyan_ds_tcn24_'+a.pair+'3':raise ValueError('Unexpected flashed model')
        saved=capture_settings(previous,voice)
        report['previous_wake']=previous;report['previous_voice']=voice
        touched=True
        link.command('agent voice off');link.command('agent cancel');link.off()
        report['previous_clip']=export_clip(link,a.out/'previous-clip.wav')
        link.command('agent wake threshold 740');link.command('agent wake gain 1')
        replay(link,a,rows,report)
        report['complete']=True
    except BaseException as error:
        report['error']=repr(error);raise
    finally:
        try:
            if touched:
                report['restored_wake']=restore_settings(link,saved)
        except BaseException as cleanup_error:
            report['complete']=False;report['cleanup_error']=repr(cleanup_error)
            raise
        finally:
            report['ended']=time.time();d.save();d.close();save(a.out/'replay.json',report)
    print(json.dumps({k:report[k] for k in ('complete','languages','negatives','resources')},ensure_ascii=False))


if __name__=='__main__':main()
