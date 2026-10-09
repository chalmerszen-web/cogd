"""Prove the frozen learned bundle's host quality before experimental flashing."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import numpy as np


def check(report,data):
    failures=[]
    if report.get('split')!='test':failures.append('No held-out test evaluation')
    for language in ('zh','yue'):
        indices=(data['label']==1)&(data['language']==language)
        ids=data['clip_id'][indices]
        if len(set(ids))<100:failures.append(language+': fewer than 100 distinct test IDs')
        metric=report['quantized']['languages'][language]
        if metric['total']!=len(ids):failures.append(language+': metric sample count mismatch')
        if metric['recall'] is None or not math.isfinite(metric['recall']) or metric['recall']<.8:
            failures.append(language+': recall below 80 percent')
        drop=report['recall_drop'][language]
        if drop is None or not math.isfinite(drop) or drop>.05000001:
            failures.append(language+': quantization drop above 5 percentage points')
    negative=report['quantized']
    if not negative['negative_clips']:failures.append('No negative clips evaluated')
    elif negative['negative_triggered_clips']>=negative['negative_clips']:failures.append('Every negative clip triggered')
    return failures


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--bundle',type=Path,required=True)
    ap.add_argument('--features',type=Path,required=True);ap.add_argument('--dataset',type=Path,required=True)
    ap.add_argument('--partial-word-gate',action='store_true',help='Phase3: <=5percent negative clips and zero partial-word triggers with coverage')
    args=ap.parse_args();path=args.bundle/'quality-gate.json'
    if path.exists():raise ValueError('Preserve completed gate evidence')
    report=json.loads((args.bundle/'test-metrics.json').read_text())
    model=json.loads((args.bundle/'model.json').read_text())
    audit=json.loads((args.dataset/'audit.json').read_text())
    metadata=json.loads((args.features/'features.json').read_text())
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    with np.load(args.features/'test.npz',allow_pickle=False) as data:failures=check(report,data)
    if args.partial_word_gate:
        q=report['quantized']
        if q['negative_triggered_clips']/max(1,q['negative_clips'])>.05:
            failures.append('Negative clip trigger rate above 5 percent')
        if q.get('partial_word_triggers',-1)!=0:failures.append('Partial-word trigger or missing partial-word metrics')
        with np.load(args.features/'test.npz',allow_pickle=False) as data:
            for language in ('zh','yue'):
                for phrase in ('你好','小言'):
                    if 'text' not in data or not np.any((data['label']==0)&(data['language']==language)&(data['text']==phrase)):
                        failures.append(language+': missing partial-word test '+phrase)
    if not audit['complete']:failures.append('Dataset audit failed')
    if audit['manifest_sha256']!=sha(args.dataset/'manifest.jsonl'):failures.append('Dataset manifest changed')
    rows=[json.loads(line) for line in (args.dataset/'manifest.jsonl').read_text(encoding='utf8').splitlines()]
    if hashlib.sha256(json.dumps(rows,sort_keys=True).encode()).hexdigest()!=metadata['dataset_sha256']:
        failures.append('Features belong to another dataset')
    if metadata['split_sha256']['test']!=sha(args.features/'test.npz'):failures.append('Prepared test features changed')
    if report['checkpoint_sha256']!=model['checkpoint_sha256']:failures.append('Checkpoint identity mismatch')
    if report['model_json_sha256']!=sha(args.bundle/'model.json'):failures.append('Model bundle changed')
    if report['evaluated_features_sha256']!=sha(args.features/'test.npz'):failures.append('Test features changed')
    if not model.get('trained') or not model.get('feature_hashes'):failures.append('Unproven trained provenance')
    from calibrate import emit_c
    if (args.bundle/'model.c').read_text()!=emit_c(model):failures.append('C export differs from evaluated model')
    output=dict(passed=not failures,failures=failures,model_c_sha256=sha(args.bundle/'model.c'),
        model_json_sha256=sha(args.bundle/'model.json'),test_report_sha256=sha(args.bundle/'test-metrics.json'),
        human_generalization_verified=False,scope='Host quality only; board parity, resources and acoustics still required')
    path.write_text(json.dumps(output,indent=2)+'\n');print(json.dumps(output,indent=2))
    if failures:raise SystemExit(2)

if __name__=='__main__':main()
