"""Validate pinned upstream inputs, documented local patch and generated probe."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

ROOT=Path(__file__).resolve().parents[2]
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    checked=0
    for package in ('kissfft','kws_ref'):
        base=ROOT/'third_party'/package
        manifest=json.loads((base/'sources.json').read_text())
        for name,sha in manifest['files'].items():
            if name in manifest.get('local_changes',{}): sha=manifest['local_changes'][name]['sha256']
            assert digest(base/name)==sha,(package,name)
            checked+=1
    # Test the documented adaptation is the sole change to the upstream header.
    base=ROOT/'third_party/kissfft'
    patch=b'''    /* Local adaptation: allow a fixed, read-only 512-point configuration. */
#ifdef KISS_FFT_STATIC_512
    kiss_fft_cpx twiddles[512];
#else
    kiss_fft_cpx twiddles[1];
#endif'''
    raw=(base/'_kiss_fft_guts.h').read_bytes()
    # Accept host line-ending normalization only while undoing the local patch.
    normalized=raw.replace(b'\r\n',b'\n')
    assert normalized.count(patch)==1
    restored=normalized.replace(patch,b'    kiss_fft_cpx twiddles[1];')
    upstream=json.loads((base/'sources.json').read_text())['files']['_kiss_fft_guts.h']
    assert hashlib.sha256(restored).hexdigest()==upstream or hashlib.sha256(restored.replace(b'\n',b'\r\n')).hexdigest()==upstream
    spec=importlib.util.spec_from_file_location('export_c',ROOT/'training/kws/export_c.py')
    exporter=importlib.util.module_from_spec(spec);spec.loader.exec_module(exporter)
    with tempfile.TemporaryDirectory(prefix='kws-verify-') as temporary:
        exporter.OUT=Path(temporary);exporter.main()
        frozen=ROOT/'components/kws_c11/generated'
        manifest=json.loads((frozen/'manifest.json').read_text())
        for name,sha in manifest['sha256'].items():
            assert digest(frozen/name)==sha and digest(exporter.OUT/name)==sha,name
            checked+=1
    print(json.dumps(dict(passed=True,checked=checked,regenerated_probe_identical=True)))

if __name__=='__main__': main()
