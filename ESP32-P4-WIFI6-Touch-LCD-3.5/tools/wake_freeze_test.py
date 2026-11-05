"""Freeze score-independent, source-held-out physical test utterances."""
import hashlib,json
from pathlib import Path
import numpy as np
from wake_prepare import ROOT,screened,pcm,bounds
BASE=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/wake_training/20261101'

def main():
    rows=list(map(json.loads,(BASE/'combined-manifest.jsonl').read_text(encoding='utf8').splitlines()))
    audit={r['clip_id']:r for r in json.loads((BASE/'combined-asr-padded.json').read_text(encoding='utf8'))['clips']}
    accepted=[]
    for r in rows:
        if r['split']!='test' or not screened(r,audit[r['clip_id']]['asr_text']):continue
        try:bounds(pcm(r['path']))
        except ValueError:continue
        accepted.append(r)
    old=list(map(json.loads,(ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl').read_text(encoding='utf8').splitlines()))
    chosen=[]
    for lang in ('zh','yue'):
        for voice in ('af_bella','am_fenrir'):
            candidates=[]
            for r in old:
                if r['split']=='test' and r['label'] and r['language']==lang and r['source_group']=='kokoro-v1-'+voice:
                    signal=pcm(r['path'])
                    if len(signal)<=32768:candidates.append(r)
            candidates.sort(key=lambda r:(r['wake_end_sample']-r['wake_start_sample'],r['clip_id']))
            assert len(candidates)>=5
            for i in np.linspace(0,len(candidates)-1,5).astype(int):chosen.append(dict(candidates[i],wake_group='nihao_'+lang))
    for voice in ('af_bella','am_fenrir'):
        candidates=[r for r in accepted if r['wake_group']=='hi_zh' and r['source_group']=='kokoro-v1-'+voice]
        assert len(candidates)>=5
        chosen+=sorted(candidates,key=lambda r:r['clip_id'])[:5]
    yue=[r for r in accepted if r['wake_group']=='hi_yue'];assert len(yue)>=6
    chosen+=sorted(yue,key=lambda r:r['clip_id'])
    for language in ('zh','yue'):
        for text in ('你好','小言','你好小燕','你好小王','嗨乐鑫','请把灯打开'):
            candidates=[r for r in old if r['split']=='test' and not r['label'] and r['language']==language and r['text']==text]
            candidates=[r for r in candidates if len(pcm(r['path']))<=48000]
            assert candidates,(language,text)
            chosen.append(dict(sorted(candidates,key=lambda r:r['clip_id'])[0],wake_group='negative'))
        candidates=[r for r in accepted if r['language']==language and not r['label']]
        chosen+=sorted(candidates,key=lambda r:r['clip_id'])[:4]
    assert len({r['clip_id'] for r in chosen})==len(chosen)
    for r in chosen:
        assert r['split']=='test'
        r['wav_sha256']=hashlib.sha256((ROOT/r['path']).read_bytes()).hexdigest()
    # Frozen order avoids grouping every language/voice into one time interval.
    order=np.random.default_rng(2026110103).permutation(len(chosen));chosen=[chosen[i] for i in order]
    path=BASE/'heldout-physical-manifest.jsonl';assert not path.exists()
    path.write_text(''.join(json.dumps(r,ensure_ascii=False)+'\n' for r in chosen),encoding='utf8')
    groups={}
    for r in chosen:
        key=r['wake_group'];g=groups.setdefault(key,{'sources':0,'voices':[]});g['sources']+=1
        if r['source_group'] not in g['voices']:g['voices'].append(r['source_group'])
    report={'manifest_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'groups':groups,'speaker_gains':[.35,.2],
        'scope':'Each source replayed at two host gains. Repetitions and synthetic voices are not independent human speakers. No measured-distance claim.',
        'selection':'Known-language ASR and complete-word duration screening only; no model scores used',
        'acceptance':'Each pronunciation >=90% over both levels, no tested negative triggers, then >=3 complete acoustic conversation turns'}
    (BASE/'heldout-physical-plan.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(report,ensure_ascii=False),flush=True)

if __name__=='__main__':main()
