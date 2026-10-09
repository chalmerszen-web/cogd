"""48 TRAIN-only physical captures, preselected independently of model scores."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import wave
from capture_corpus import balanced

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/kws-phase5'


def main():
    rows=[json.loads(line) for line in (ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl').read_text(encoding='utf8').splitlines()]
    used=set()
    for directory in ('corpus','corpus-zh-negatives'):
        used.update(t['source']['clip_id'] for t in json.loads((OUT/directory/'report.json').read_text(encoding='utf8'))['trials'])
    eligible=[]
    for row in rows:
        if row['split']!='train' or row['clip_id'] in used or not row['source_group'].startswith('kokoro-'):continue
        with wave.open(str(ROOT/row['path']),'rb') as f:seconds=f.getnframes()/f.getframerate()
        if seconds<=3.5:eligible.append(dict(row,actual_seconds=seconds))
    sources=[]
    for language in ('zh','yue'):
        positives=balanced([r for r in eligible if r['language']==language and r['label']])
        assert len(positives)>=8;sources.extend(positives[:8])
        for text in ('你好','小言'):
            partial=balanced([r for r in eligible if r['language']==language and not r['label'] and r['text']==text])
            assert len(partial)>=2;sources.extend(partial[:2])
    assert len(sources)==24 and len({r['clip_id'] for r in sources})==24
    selected=[]
    # Alternate tiers/voices, retaining original parent identity and same split.
    for index,row in enumerate(sources):
        for gain,tag in ((.35,'normal'),(.35*10**(-12/20),'minus12')):
            selected.append(dict(row,clip_id=row['clip_id']+'-capture-'+tag,
                parent_clip_id=row['clip_id'],playback_gain=gain,
                source_wav_sha256=hashlib.sha256((ROOT/row['path']).read_bytes()).hexdigest()))
    manifest=OUT/'full-phrase-supplement-selection.json'
    with manifest.open('x',encoding='utf8') as f:json.dump(selected,f,ensure_ascii=False,indent=2)
    plan=dict(max_captures=48,unique_sources=24,train_only=True,model_score_selection=False,
        gain=[.35,.35*10**(-12/20)],five_metre_equivalence=False,
        reason='New train-source positive and standalone partial recordings after generalization diagnostic',
        sources_sha256=hashlib.sha256(manifest.read_bytes()).hexdigest())
    with (OUT/'full-phrase-supplement-plan.json').open('x',encoding='utf8') as f:json.dump(plan,f,indent=2)
    destination=OUT/'corpus-full-phrase'
    command=[sys.executable,'-X','utf8',str(ROOT/'tools/kws/capture_corpus.py'),
        '--out',str(destination),'--selection',str(manifest),'--version','0.7.0-xiaoyan-exp']
    with (OUT/'capture-full-phrase.log').open('xb') as log:
        result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT)
    if destination.exists():shutil.copyfile(ROOT/'tools/kws/capture_corpus.py',destination/'capture_corpus.py')
    if result.returncode:raise RuntimeError('Capture failed; keep incomplete report, do not replay automatically')
    report=json.loads((destination/'report.json').read_text(encoding='utf8'))
    assert report['complete'] and len(report['trials'])==48
    print(json.dumps(dict(complete=True,captures=48,training_started=False)),flush=True)


if __name__=='__main__':main()
