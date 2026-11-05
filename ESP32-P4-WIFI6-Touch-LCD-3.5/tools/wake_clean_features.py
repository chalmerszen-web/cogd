"""TRAIN-only ablation: complete phonetic-audited sources plus old background.

Time cuts and unverified legacy speech are excluded, not relabeled. This tests
whether old scalar labels interfere with soft onset/near-tone discrimination.
"""
import argparse,hashlib,json,re,shutil
from pathlib import Path
import numpy as np

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--prior',type=Path,required=True);p.add_argument('--sources',type=Path,nargs='+',required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    sources={}
    for path in a.sources:
        for line in path.read_text(encoding='utf8').splitlines():
            r=json.loads(line);assert r['split']=='train' and r.get('phonetic_tokens');assert r['clip_id'] not in sources;sources[r['clip_id']]=r
    pattern=re.compile(r'wide95(?:b|-soft31)?-train-[A-Za-z0-9_.]+-\d{4}');counts={};audit=[]
    for split in ('train','validation'):
        d=dict(np.load(a.prior/(split+'.npz'),allow_pickle=False));mask=np.zeros(len(d['x']),bool);background=0;admitted=set()
        for i,clip in enumerate(d['clip_id']):
            found=pattern.search(str(clip));source=sources.get(found.group(0)) if found else None
            if source and bool(d['label'][i])==bool(source['label']):
                assert str(d['text'][i])==source['text'];mask[i]=True;admitted.add(source['clip_id'])
            elif not d['label'][i] and not str(d['text'][i]).strip():
                mask[i]=True;background+=1
        assert mask.any();selected={key:value[mask] for key,value in d.items()};np.savez_compressed(a.out/(split+'.npz'),**selected)
        np.save(a.out/(split+'-parent-indices.npy'),np.flatnonzero(mask))
        counts[split]={'original':len(mask),'selected':int(mask.sum()),'excluded':int((~mask).sum()),'retained_background':background,
                       'groups':{str(g):int(sum(selected['wake_group']==g)) for g in np.unique(selected['wake_group'])}}
        audit.append({'split':split,'source_ids':sorted(admitted),'parent_sha256':sha(a.prior/(split+'.npz')),'derived_sha256':sha(a.out/(split+'.npz'))})
    shutil.copy2(a.prior/'normalization.json',a.out/'normalization.json')
    meta=json.loads((a.prior/'features.json').read_text(encoding='utf8'))
    meta.update(round2_audio_used_for_training=False,phonetic_complete_ablation=True,previous_weights_may_include_retired_old_rounds=True,
                source_hashes={str(path):sha(path) for path in a.sources},counts=counts,selection=audit,
                no_dev_or_final_sources=True,no_source_relabeling=True,partial_cuts_excluded=True,source_sha256=sha(__file__))
    (a.out/'features.json').write_text(json.dumps(meta,indent=2),encoding='utf8');print(json.dumps(counts),flush=True)

if __name__=='__main__':main()
