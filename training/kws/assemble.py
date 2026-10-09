"""Freeze screened source manifests; validate samples before allocating features."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
from data import read_manifest, load_pcm


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('manifests',type=Path,nargs='+')
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--minimum-test',type=int,default=100)
    args=ap.parse_args()
    args.out.mkdir(parents=True,exist_ok=False)
    merged=args.out/'manifest.jsonl'
    text=''.join(path.read_text(encoding='utf8').rstrip()+'\n' for path in args.manifests)
    merged.write_text(text,encoding='utf8')
    rows=read_manifest(merged)
    counts=Counter();seconds=Counter();voices={};hashes={}
    for row in rows:
        pcm=load_pcm(row)
        key=f"{row['split']}/{row['language']}/{row['label']}"
        counts[key]+=1;seconds[key]+=len(pcm)/16000
        voices.setdefault(key,set()).add(row['source_group'])
        hashes.setdefault(key,set()).add(row['pcm_sha256'])
    failures=[]
    for language in ('zh','yue'):
        for split in ('train','validation','test'):
            key=f'{split}/{language}/1'
            minimum=args.minimum_test if split=='test' else 1
            if len(hashes.get(key,set()))<minimum:failures.append(f'{key}: fewer than {minimum} unique positive PCM files')
    for split in ('train','validation','test'):
        if not any(r['split']==split and not r['label'] for r in rows):failures.append(f'{split}: missing negatives')
    report=dict(complete=not failures,failures=failures,rows=len(rows),counts=dict(counts),
        unique_pcm={k:len(v) for k,v in hashes.items()},source_groups={k:sorted(v) for k,v in voices.items()},
        original_audio_seconds=dict(seconds),manifest_sha256=hashlib.sha256(merged.read_bytes()).hexdigest(),
        inputs=[dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in args.manifests],
        evidence='Distinct generated files are not distinct human speakers. Original audio duration is not evaluated listening time.')
    (args.out/'audit.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf8')
    print(json.dumps(report,indent=2,ensure_ascii=False))
    if failures:raise SystemExit(2)

if __name__=='__main__':main()
