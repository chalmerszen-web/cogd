"""Freeze every quality-approved new round-three recording before KWS scoring."""
import hashlib,json
from pathlib import Path
import numpy as np
from wake_prepare import ROOT,screened,pcm,bounds
B=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/wake_training/20261101'

def read(path):return json.loads(path.read_text(encoding='utf8'))
def lines(path):return [json.loads(s) for s in path.read_text(encoding='utf8').splitlines()]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    selected=[];rejected=[];excluded_generation=[]
    for manifest,audit in ((B/'round3-synthetic/manifest.jsonl',B/'round3-zh-asr.json'),
                           (B/'qwen-native-round3/manifest.jsonl',B/'native-round3-asr.json')):
        checks={r['clip_id']:r['asr_text'] for r in read(audit)['clips']}
        rows=lines(manifest)
        if 'synthetic' in str(manifest):assert len(rows)==30
        for row in rows:
            if not screened(row,checks[row['clip_id']]):
                rejected.append({'id':row['clip_id'],'reason':'ASR not confirmed','asr':checks[row['clip_id']]});continue
            try:bounds(pcm(row['path']))
            except ValueError as error:
                rejected.append({'id':row['clip_id'],'reason':str(error)});continue
            selected.append(row)
    assert len(lines(B/'round3-carrier/manifest.jsonl'))==30
    selected+=lines(B/'round3-yue-curated/manifest.jsonl')
    jobs=read(B/'qwen-native-round3/jobs.json');assert len(jobs)==18
    native_ids={r['clip_id'] for r in lines(B/'qwen-native-round3/manifest.jsonl')}
    for row in jobs:
        state=read(ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/.local/wake4-qwen-tts'/row['clip_id']/'state.json')
        if state['status']=='succeeded':assert row['clip_id'] in native_ids
        else:
            assert state['status']=='acceptance_unknown'
            excluded_generation.append({'id':row['clip_id'],'status':'acceptance_unknown','resubmitted':False})
    known=set();known_ids=set()
    for manifest in (B/'combined-manifest.jsonl',B/'expanded-train-round1/manifest.jsonl',
                     B/'qwen-native-joint-train/manifest.jsonl',B/'native-joint-augmented/manifest.jsonl',
                     B/'heldout-physical-manifest.jsonl',B/'heldout-round2-manifest.jsonl'):
        for r in lines(manifest):
            known.add(hashlib.sha256(pcm(r['path']).tobytes()).hexdigest());known_ids.add(r['clip_id'])
    assert len({r['clip_id'] for r in selected})==len(selected)
    for row in selected:
        assert row['split']=='test' and '-r3-' in row['clip_id']
        assert row['source_group'] in ('kokoro-v1-af_nicole','kokoro-v1-am_adam','qwen3-system-Rocky','qwen3-system-Kiki')
        digest=hashlib.sha256(pcm(row['path']).tobytes()).hexdigest()
        assert digest not in known and row['clip_id'] not in known_ids
        row['wav_sha256']=sha(ROOT/row['path'])
    ancestry=read(B/'round2-pretrained-ancestry.json')
    checks=[]
    for features in sorted({r['features'] for r in ancestry['checkpoints']}|{(B/'features-v10').relative_to(ROOT).as_posix()}):
        with np.load(ROOT/features/'train.npz',allow_pickle=False) as data:
            # The older 48-channel feature pack identifies voices in clip_id.
            # Check every string column and require identity-bearing metadata.
            columns=[k for k in data.files if data[k].dtype.kind in 'US']
            assert 'source_group' in columns or 'clip_id' in columns
            matches=[s for k in columns for s in set(map(str,data[k])) if 'af_nicole' in s or 'am_adam' in s]
            assert not matches,(features,matches)
            checks.append({'features':features,'train_sha256':sha(ROOT/features/'train.npz'),'string_columns_checked':columns,'matches':matches})
    order=np.random.default_rng(2026110123).permutation(len(selected));selected=[selected[i] for i in order]
    out=B/'heldout-round3-manifest.jsonl';assert not out.exists()
    out.write_text(''.join(json.dumps(r,ensure_ascii=False)+'\n' for r in selected),encoding='utf8')
    groups={}
    for row in selected:
        g=groups.setdefault(row['wake_group'],{'sources':0,'voices':{}});g['sources']+=1
        g['voices'][row['source_group']]=g['voices'].get(row['source_group'],0)+1
    assert all(groups[g]['sources']>=5 for g in ('nihao_zh','nihao_yue','hi_zh','hi_yue','negative'))
    scopes={}
    for group,values in groups.items():
        kinds=set('known' if v.startswith('qwen3') else 'untrained' for v in values['voices'])
        scopes[group]='new recordings of TRAIN native voices; NOT speaker-disjoint' if kinds=={'known'} else 'untrained af_nicole/am_adam voices' if kinds=={'untrained'} else 'mix of untrained voices and new recordings of TRAIN native voices'
    plan={'manifest_sha256':sha(out),'source_count':len(selected),'groups':groups,'group_scope':scopes,
          'speaker_gains':[.35,.20],'selection':'ALL automatic ASR/duration-approved new recordings; no KWS score selection',
          'acceptance':'Each positive group >=90% across both gains; zero tested negatives; regression, four complete dialogues, external audio, Flash readback',
          'prior_rounds':'Failed rounds one and two retained; round-two audio excluded from training but used for objective diagnosis',
          'excluded_generation':excluded_generation,'rejected_quality_only':rejected,
          'scope':'Fixed synthetic acoustic playback; no human or measured-distance success-rate claim'}
    (B/'heldout-round3-plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2),encoding='utf8')
    (B/'round3-independence-check.json').write_text(json.dumps({'complete':True,'manifest_sha256':sha(out),'new_recording_overlap':0,'training_features_checked':checks,'native_voice_overlap':'Known Rocky/Kiki identities, only recordings are held out'},indent=2),encoding='utf8')
    print(json.dumps(plan,ensure_ascii=False))

if __name__=='__main__':main()
