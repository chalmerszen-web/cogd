"""Freeze finite device-replay sources and record candidate regression examples."""
import hashlib
import json
from pathlib import Path
import sys
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/kws'),str(ROOT/'training/kws')]
from append_captures import capture_path
from evaluate import events,threshold_q8


def main():
    base=ROOT/'artifacts/kws-bilingual'
    data=np.load(base/'features-compact/validation.npz')
    before=np.load(base/'ek-scores.npz')['quantized']
    after=np.load(base/'el-scores.npz')['quantized']
    changed=[]
    for i in range(len(data['x'])):
        a,b=events(before[i],threshold_q8(740)),events(after[i],threshold_q8(740))
        if bool(a)==bool(b):continue
        changed.append(dict(clip_id=str(data['clip_id'][i]),source_group=str(data['source_group'][i]),
            label=int(data['label'][i]),language=str(data['language'][i]),text=str(data['text'][i]),
            before=a,after=b))
    (base/'changed-trigger-clips.json').write_text(json.dumps(changed,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    sapi=json.loads((base/'sapi-probe-02/baseline-deduplicated.json').read_text(encoding='utf8'))
    wanted=['sapi-huihui-02','sapi-huihui-03','sapi-huihui-05','sapi-kangkang-03',
            'sapi-huihui-06','sapi-huihui-07','sapi-kangkang-08','sapi-kangkang-12']
    rows=[]
    for ident in wanted:
        source=next(r for r in sapi['trials'] if r['clip_id']==ident)
        path=capture_path(source['path']);assert hashlib.sha256(path.read_bytes()).hexdigest()==source['wav_sha256']
        row={k:source[k] for k in ('clip_id','text','language','label','source_group','wav_sha256')}
        row.update(path=path.relative_to(ROOT).as_posix(),split='validation',seconds=source['samples']/16000,
            wake_start_sample=source['energy_start'],wake_end_sample=source['energy_end'],
            target_end_low_sample=source['energy_end']-320,target_end_high_sample=source['energy_end']+320,
            annotation='Fixed energy endpoint, not confirmed phonetic boundary',source_kind='local SAPI synthetic, diagnostic only')
        rows.append(row)
    phase6=[json.loads(line) for line in (ROOT/'artifacts/kws-phase6/replay-selection.jsonl').read_text(encoding='utf8').splitlines()]
    rows.extend(r for r in phase6 if r['language']=='yue' and (r['label'] or r['text'] in ('你好','你好小燕')))
    assert len(rows)==14 and sum(r['label'] for r in rows)==8
    out=base/'replay-selection.jsonl'
    if out.exists():raise ValueError('Preserve frozen replay selection')
    out.write_text(''.join(json.dumps(r,ensure_ascii=False)+'\n' for r in rows),encoding='utf8')
    print(json.dumps(dict(changed=changed,replay_count=len(rows),replay_sha256=hashlib.sha256(out.read_bytes()).hexdigest()),ensure_ascii=False))


if __name__=='__main__':main()
