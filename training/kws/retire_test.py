"""Explicitly retire a failed blind set before a preregistered final attempt.

This does not create or score the new test set. Its source groups must be fresh.
"""
import argparse
import hashlib
import json
from pathlib import Path
from data import read_manifest

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--previous',type=Path,required=True)
    ap.add_argument('--report',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args();report=json.loads(args.report.read_text())
    if report.get('split')!='test':raise ValueError('Expected the previously consumed test evaluation')
    rows=read_manifest(args.previous);changes=[]
    args.out.mkdir(parents=True,exist_ok=False)
    for row in rows:
        if row['split']=='test':
            target='train' if row['source_group'].startswith('kokoro-v1-') else 'validation'
            row.update(previous_split='test',split=target,
                       disposition='Unblinded after failed adjustment1; never count again as independent test')
            changes.append(dict(clip_id=row['clip_id'],source_group=row['source_group'],new_split=target))
    manifest=args.out/'manifest.jsonl'
    manifest.write_text(''.join(json.dumps(row,ensure_ascii=False)+'\n' for row in rows),encoding='utf8')
    read_manifest(manifest)
    audit=dict(previous_manifest_sha256=hashlib.sha256(args.previous.read_bytes()).hexdigest(),
        consumed_test_report_sha256=hashlib.sha256(args.report.read_bytes()).hexdigest(),
        reassigned_sources=changes,independent_test_rows=0)
    (args.out/'retirement.json').write_text(json.dumps(audit,indent=2)+'\n')
    print(json.dumps(dict(rows=len(rows),reassigned=len(changes),independent_test_rows=0)))

if __name__=='__main__':main()
