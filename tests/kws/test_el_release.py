"""Source-only candidate can regenerate its C weights and retain old models."""
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from calibrate import emit_c


def test_exported_l_and_retained_ek_match_release_provenance():
    base=ROOT/'training/kws/releases/compact-l'
    generated=ROOT/'components/kws_c11/generated'
    manifest=json.loads((base/'manifest.json').read_text(encoding='utf8'))
    digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    assert manifest['default_pair']=='ek' and manifest['pair']=='el'
    assert digest(generated/'fusion_secondary_l.json')==manifest['model_json_sha256']
    assert digest(generated/'fusion_secondary_l.c')==manifest['c_sha256']
    for name,expected in manifest['retained_model_sha256'].items():assert digest(generated/name)==expected
    for name,expected in manifest['files'].items():assert digest(base/name)==expected
    model=json.loads((generated/'fusion_secondary_l.json').read_text())
    assert model['checkpoint_sha256']==manifest['checkpoint_sha256']
    assert emit_c(model,'kws_secondary_model')==(generated/'fusion_secondary_l.c').read_text()
