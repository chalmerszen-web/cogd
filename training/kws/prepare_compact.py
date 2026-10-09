"""Append screened no-pause Mandarin examples with fixed old normalization."""
import hashlib
import json
from pathlib import Path
import shutil
import numpy as np

from data import ROOT, Frontend, framed_example, normalize, load_pcm
from boundary import interval
from append_captures import capture_path
from device_domain import pcm
from paired_negative import incomplete


def main():
    base=ROOT/'artifacts/kws-phase5/features-weak-logmel'
    out=ROOT/'artifacts/kws-bilingual/features-compact'
    source=ROOT/'artifacts/kws-bilingual/compact-tts-02/data/dataset-zh-compact2.jsonl'
    audit=ROOT/'artifacts/kws-bilingual/compact-asr.json'
    rows=[json.loads(line) for line in source.read_text(encoding='utf8').splitlines()]
    transcripts={r['clip_id']:r['asr_text'] for r in json.loads(audit.read_text(encoding='utf8'))['clips']}
    # Mandarin homophones preserve xiao3 yan2. Different consonants/nasal finals
    # and missing words are rejected; this is machine screening, not human review.
    accepted={'你好小言','你好小严','你好小妍','你好小颜','你好小岩','你好小炎','你好小研'}
    stats=json.loads((base/'normalization.json').read_text())
    frontend=Frontend();rng=np.random.default_rng(202609221)
    capmeta=json.loads((ROOT/'artifacts/kws-phase5/features/features.json').read_text())
    noise=[pcm(capture_path(c['recording']))[-16000:] for c in capmeta['captures'] if c['split']=='train']
    assert len(noise)>=20
    original={split:np.load(base/(split+'.npz')) for split in ('train','validation')}
    examples={s:[] for s in original};rejected=[];ancestry=[]
    for row in rows:
        cleaned=''.join(c for c in transcripts[row['clip_id']] if '\u4e00'<=c<='\u9fff')
        if cleaned not in accepted:
            rejected.append(dict(clip_id=row['clip_id'],reason='asr_not_complete_target',transcript=transcripts[row['clip_id']]))
            continue
        raw=load_pcm(row)
        try:bounds=interval(raw)
        except ValueError as error:
            rejected.append(dict(clip_id=row['clip_id'],reason=str(error)));continue
        split=row['split'];assert split in examples and row['language']=='zh'
        assert row['source_group'] in set(original[split]['source_group'])
        moved=dict(row,**bounds,endpoint_schema=1,clip_id='compact-'+row['clip_id'],sampling_group='positive')
        variants=[(moved,raw)]
        if split=='train':
            for side in ('prefix','suffix'):
                part,_=incomplete(raw,bounds['speech_start_sample'],bounds['speech_end_low_sample'],side)
                variants.append((dict(moved,clip_id=moved['clip_id']+'-'+side,label=0,text='time-cut-'+side,sampling_group='paired_negative'),part))
        for variant_row,signal in variants:
            for variant in range(9 if split=='train' else 1):
                background=noise[int(rng.integers(len(noise)))] if variant else None
                wave,labels,end=framed_example(variant_row,signal,rng if variant else None,background=background)
                examples[split].append((variant_row,normalize(frontend(wave),stats),labels,end,variant))
        ancestry.append(dict(clip_id=moved['clip_id'],source_group=row['source_group'],split=split,
            pcm_sha256=row['pcm_sha256'],transcript=transcripts[row['clip_id']],**bounds))
    assert sum(r['split']=='train' for r in ancestry)>=16
    assert sum(r['split']=='validation' for r in ancestry)>=8
    out.mkdir(parents=True,exist_ok=False)
    for split,old in original.items():
        added=examples[split]
        extra=dict(x=np.stack([e[1] for e in added]),y=np.stack([e[2] for e in added]),
            label=np.array([e[0]['label'] for e in added]),language=np.array([e[0]['language'] for e in added]),
            clip_id=np.array([e[0]['clip_id'] for e in added]),source_group=np.array([e[0]['source_group'] for e in added]),
            event_end=np.array([e[3] if e[3] is not None else -1 for e in added]),
            event_start_accept=np.array([e[3]-(e[0]['speech_end_high_sample']-e[0]['speech_end_low_sample']) if e[3] is not None else -1 for e in added]),
            sampling_group=np.array([e[0]['sampling_group'] for e in added]),text=np.array([e[0]['text'] for e in added]),
            variant=np.array([e[4] for e in added]),device_domain=np.zeros(len(added),dtype=bool))
        assert set(extra)==set(old.files)
        np.savez_compressed(out/(split+'.npz'),**{k:np.concatenate((old[k],extra[k])) for k in extra})
    for name in ('normalization.json','test.npz'):shutil.copyfile(base/name,out/name)
    report=dict(complete=True,seed=202609221,base=str(base),sources=ancestry,rejected=rejected,
        examples={s:len(v) for s,v in examples.items()},old_prefix_unchanged=True,
        new_validation='Existing validation source groups only; SAPI material excluded from training/selection.',
        source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),audit_sha256=hashlib.sha256(audit.read_bytes()).hexdigest(),
        split_sha256={s:hashlib.sha256((out/(s+'.npz')).read_bytes()).hexdigest() for s in ('train','validation','test')})
    (out/'features.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps({k:report[k] for k in ('complete','examples','rejected')},ensure_ascii=False))


if __name__=='__main__':main()
