"""Reconstruct only the two frozen E validation prefix failures, no rescoring."""
import hashlib
import json
from pathlib import Path
import sys
import wave
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from device_domain import pcm
from paired_negative import incomplete


def main():
    base=ROOT/'artifacts/kws-phase5';out=base/'remaining-prefixes';out.mkdir(exist_ok=False)
    meta=json.loads((base/'features/features.json').read_text(encoding='utf8'))
    rows={r['clip_id']:r for r in map(json.loads,(ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl').read_text(encoding='utf8').splitlines())}
    chosen=('device-dataset-zh-zf_xiaoxiao-1-004-bounded1','device-dataset-yue-zm_yunxia-1-009-bounded1')
    manifest=[];audit=[]
    for capture in meta['captures']:
        if capture['clip_id'] not in chosen:continue
        row=rows[capture['parent_clip_id']];recorded=pcm(ROOT/capture['recording']);lo,hi=capture['crop']
        full=recorded[lo:hi];start=max(0,capture['lag_samples']-lo+row['speech_start_sample'])
        prefix,cut=incomplete(full,start,capture['end_interval'][0],'prefix',ambient=recorded[-16000:])
        for tag,samples in (('full',full),('prefix',prefix),('removed-suffix',full[cut:])):
            path=out/(row['language']+'-'+tag+'.wav')
            with wave.open(str(path),'wb') as f:
                f.setparams((1,2,16000,0,'NONE','not compressed'));f.writeframes(samples.astype('<i2').tobytes())
            manifest.append(dict(clip_id=capture['clip_id']+'-'+tag,path=path.as_posix(),language=row['language'],text='你好，小言' if tag=='full' else 'diagnostic partial'))
        audit.append(dict(clip_id=capture['clip_id'],capture=capture,cut=cut,start=start,
            full_samples=len(full),prefix_sha256=hashlib.sha256(prefix.tobytes()).hexdigest(),
            frozen_model='adapt-e',no_new_inference=True))
    assert len(audit)==2
    with (out/'manifest.jsonl').open('w',encoding='utf8') as f:
        for row in manifest:f.write(json.dumps(row,ensure_ascii=False)+'\n')
    (out/'reconstruction.json').write_text(json.dumps(audit,ensure_ascii=False,indent=2),encoding='utf8')


if __name__=='__main__':main()
