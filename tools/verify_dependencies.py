"""Verify the small vendored runtime before building; no downloads or secrets."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
manifest = json.loads((root/'third_party/manifest.json').read_text(encoding='utf8'))
for name, expected in manifest['files'].items():
    path = root/'third_party'/name
    assert path.is_file(), 'Missing runtime dependency: '+name
    assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, 'Changed runtime dependency: '+name
print(json.dumps(dict(passed=True, files=len(manifest['files']))))
