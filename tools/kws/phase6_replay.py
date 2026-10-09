"""Fixed synthetic-source device replay, including confusable negatives at every level."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tools/kws')]
from acoustic_test import save


def ordered(rows):
    return sorted(rows,key=lambda r:hashlib.sha256(('phase6-v1:'+r['clip_id']).encode()).hexdigest())


def selection(base):
    manifest=base/'replay-selection.jsonl'
    if manifest.exists():
        rows=[json.loads(line) for line in manifest.read_text(encoding='utf8').splitlines()]
        assert len(rows)==20
        return manifest
    rows=[json.loads(line) for line in (ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl').read_text(encoding='utf8').splitlines()]
    rows=[r for r in rows if r['split']=='validation' and r['source_group'].startswith('kokoro-v1-')]
    seen=set()
    for path in (ROOT/'artifacts/kws-phase5').rglob('*selection.json'):
        data=json.loads(path.read_text(encoding='utf8'))
        if isinstance(data,list):
            seen.update(r.get('parent_clip_id',r.get('clip_id')) for r in data if isinstance(r,dict))
    chosen=[]
    for lang in ('zh','yue'):
        positives=[r for r in rows if r['label'] and r['language']==lang and r['clip_id'] not in seen]
        groups=sorted({r['source_group'] for r in positives})
        assert len(groups)==2
        remaining={group:ordered([r for r in positives if r['source_group']==group]) for group in groups}
        assert sum(map(len,remaining.values()))>=4
        selected=[]
        while len(selected)<4:
            for group in groups:
                if remaining[group] and len(selected)<4:selected.append(remaining[group].pop(0))
        chosen.extend(selected)
        for phrase in ('你好','小言','你好小燕','你好小王','嗨乐鑫','请把灯打开'):
            candidates=ordered([r for r in rows if not r['label'] and r['language']==lang and r['text']==phrase])
            if not candidates:raise ValueError('Missing fixed negative '+lang+'/'+phrase)
            chosen.append(candidates[0])
    assert len(chosen)==20 and len({r['clip_id'] for r in chosen})==20
    for row in chosen:
        row['source_wav_sha256']=hashlib.sha256((ROOT/row['path']).read_bytes()).hexdigest()
    with manifest.open('x',encoding='utf8') as f:
        for row in chosen:f.write(json.dumps(row,ensure_ascii=False)+'\n')
    save(base/'replay-plan.json',dict(attenuation_db=[0,6,12],baseline_gain=.35,per_language=4,negatives=12,
        trials_total=60,selection_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest(),
        new_positive_replay_sources=True,selection_uses_model_scores=False,
        threshold_retuning=False,includes_near_words_at_every_level=True,
        limitation='Development validation TTS voices, fresh physical runs; not blind human or measured distance evidence.'))
    return manifest


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--pair',choices=('ef','ek'),default='ef')
    args=parser.parse_args()
    base=ROOT/'artifacts/kws-phase6';base.mkdir(exist_ok=True)
    manifest=selection(base)
    rows=[json.loads(line) for line in manifest.read_text(encoding='utf8').splitlines()]
    for row in rows:
        assert hashlib.sha256((ROOT/row['path']).read_bytes()).hexdigest()==row['source_wav_sha256']
    out=base/('replay-'+args.pair);out.mkdir(exist_ok=False)
    threshold=ROOT/'artifacts/kws-phase5'/('fusion-'+args.pair+'-smooth3')/'threshold.json'
    from serial_test import Link
    from calibrate_acoustic import export_clip
    from wave_play import output_devices
    outputs=output_devices()
    assert sum('Misiom-Shooter' in name for _,name in outputs)==1
    link=Link('COM5',out/'preflight-serial.log')
    try:
        wake=link.command('agent wake status')
        expected='xiaoyan_ds_tcn24_'+args.pair+'3'
        assert wake['model']==expected,(wake['model'],expected)
        previous=export_clip(link,out/'previous-clip.wav')
        save(out/'preflight.json',dict(wake=wake,outputs=outputs,previous_clip=previous))
    finally:link.close()
    results=[]
    for db in (0,6,12):
        directory=out/f'minus{db:02d}'
        command=[sys.executable,'-X','utf8',str(ROOT/'tools/kws/acoustic_test.py'),'replay',
            '--manifest',str(manifest),'--threshold',str(threshold),'--out',str(directory),
            '--split','validation','--per-language','4','--negatives','12',
            '--gain',str(.35*10**(-db/20)),'--source-rms','.14','--meter']
        with (out/f'minus{db:02d}.log').open('xb') as f:
            process=subprocess.run(command,stdout=f,stderr=subprocess.STDOUT)
        if process.returncode:raise RuntimeError('Physical replay failed; keep evidence, no automatic restart')
        report=json.loads((directory/'replay.json').read_text(encoding='utf8'))
        assert report['complete'] and all(t['observed_after_playback_ms']>=1200 for t in report['trials'])
        assert not report['resources']['failures']
        result=dict(attenuation_db=db,languages=report['languages'],negatives=report['negatives'],
                    near_words=[dict(language=t['language'],text=t['text'],triggered=t['triggered'])
                                for t in report['trials'] if not t['label'] and t['text'].startswith('你好小')],
                    resources=report['resources'])
        results.append(result);save(out/'results.json',results)
        print(json.dumps(result,ensure_ascii=False),flush=True)


if __name__=='__main__':main()
