"""One fixed I adaptation plus predeclared E/I pairing; no device mutations."""
import hashlib
import json
from pathlib import Path

from train_device_domain import ROOT, OUT, run


def main():
    audit=json.loads((OUT/'word-contrast-audit.json').read_text(encoding='utf8'))
    assert audit['complete'] and sum(r['eligible'] for r in audit['trials'])==28
    assert not (OUT/'adapt-i').exists()
    sources=list((ROOT/'training/kws').glob('*.py'))+[Path(__file__),ROOT/'tools/kws/evaluate_joint_fusion.py']
    freeze={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    with (OUT/'adapt-i-code-freeze.json').open('x') as f:json.dump(freeze,f,indent=2)
    features='artifacts/kws-phase5/features-word-contrast'
    run('prepare-word-contrast','training/kws/append_captures.py',
        '--corpus','artifacts/kws-phase5/corpus-word-contrast',
        '--audit','artifacts/kws-phase5/word-contrast-audit.json',
        '--base','artifacts/kws-phase5/features-measured-v2','--seed','20261003','--out',features)
    run('audit-word-features','tools/kws/audit_word_features.py')
    run('train-adapt-i','training/kws/train.py','--features',features,
        '--out','artifacts/kws-phase5/adapt-i','--initial-checkpoint','artifacts/kws-phase5/adapt-f/best.pt',
        '--steps','2000','--batch','32','--threads','4','--seed','20261003','--learning-rate','.0001',
        '--paired-negatives','--contrast-negatives','--device-fraction','.25')
    run('calibrate-adapt-i','training/kws/calibrate.py','--features',features,
        '--checkpoint','artifacts/kws-phase5/adapt-i/best.pt','--out','artifacts/kws-phase5/adapt-i-int8')
    run('validate-adapt-i','training/kws/evaluate.py','--features',features,
        '--checkpoint','artifacts/kws-phase5/adapt-i/best.pt','--bundle','artifacts/kws-phase5/adapt-i-int8',
        '--split','validation')
    run('validate-fusion-ei','tools/kws/evaluate_joint_fusion.py','--secondary','adapt-i','--smoothing-blocks','3')
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==sha for p,sha in freeze.items())
    gates=json.loads((OUT/'fusion-ei-smooth3/validation-metrics.json').read_text())['host_gate']
    (OUT/'adapt-i-finished.json').write_text(json.dumps(dict(complete=True,steps=2000,
        gates=gates,original_validation_passed=all(gates.values()),test_evaluated=False,flashed=False),indent=2))


if __name__=='__main__':main()
