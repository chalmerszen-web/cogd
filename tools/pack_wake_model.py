"""Pack one pinned ESP-SR model as aligned, read-only application data."""
from pathlib import Path
import hashlib
import json
import struct
import sys

root, output = map(Path, sys.argv[1:3])
name = sys.argv[3] if len(sys.argv)>3 else 'wn9s_hilexin'
assert name in ('wn9s_nihaoxiaozhi','wn9s_hiesp','wn9s_hilexin','wn9s_alexa','wn9s_hijason','wn10_nihaoxiaozhi')
source = root / 'model/wakenet_model' / name
if name == 'wn10_nihaoxiaozhi':
    # Match ESP-SR movemodel.copy_wn10_model: P4 uses PIE p2, S3 uses p1.
    # Select the unmodified P4 weights and expose the loader's canonical names.
    files = [(source / '_MODEL_INFO_p2', '_MODEL_INFO_'),
             (source / 'wn10_data_p2', 'wn10_data')]
else:
    files = [(p, p.name) for p in sorted(source.iterdir()) if p.is_file()]
    assert {n for _, n in files} == {'_MODEL_INFO_', 'wn9_data', 'wn9_index'}
header = bytearray(struct.pack('<I32sI', 1, name.encode(), len(files)))
payload = bytearray()
offset = 40 + len(files) * 40
hashes = {}
for path, packed_name in files:
    data = path.read_bytes()
    padding = (-offset) % 16
    payload.extend(bytes(padding))
    offset += padding
    header.extend(struct.pack('<32sII', packed_name.encode(), offset, len(data)))
    payload.extend(data)
    offset += len(data)
    hashes[path.name] = hashlib.sha256(data).hexdigest()
output.write_bytes(header + payload)
output.with_suffix('.json').write_text(json.dumps({'model': name, 'files': hashes,
    'bytes': output.stat().st_size, 'sha256': hashlib.sha256(output.read_bytes()).hexdigest()}, indent=2))
