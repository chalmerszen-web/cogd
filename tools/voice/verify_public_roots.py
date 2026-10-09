"""Check pinned IDF common roots against configured public providers.

TLS handshakes only: no credentials, application request, retry or system-root
fallback. This host check complements device TLS tests; it cannot replace them.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import socket
import ssl
import sys

ROOT = Path(__file__).resolve().parents[2]
BUNDLE = ROOT / '.toolchains/esp-idf/components/mbedtls/esp_crt_bundle'


def verify(out):
    out.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(BUNDLE))
    from gen_crt_bundle import CertificateBundle, serialization
    bundle = CertificateBundle()
    bundle.add_with_filter(str(BUNDLE/'cacrt_all.pem'), str(BUNDLE/'cmn_crt_authorities.csv'))
    pem = b''.join(c.public_bytes(serialization.Encoding.PEM) for c in bundle.certificates)
    binary = bundle.create_bundle()
    (out/'common-roots.pem').write_bytes(pem)
    (out/'common-roots.bin').write_bytes(binary)
    report = dict(timestamp=datetime.now(timezone.utc).isoformat(),
                  certificates=len(bundle.certificates), binary_bytes=len(binary),
                  binary_sha256=hashlib.sha256(binary).hexdigest(),
                  filter_sha256=hashlib.sha256((BUNDLE/'cmn_crt_authorities.csv').read_bytes()).hexdigest(),
                  system_roots=False, retries=0, application_requests=0, complete=False, hosts=[])
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.load_verify_locations(cadata=pem.decode('ascii'))
    assert context.verify_mode == ssl.CERT_REQUIRED and context.check_hostname
    try:
        for host in ('api.deepseek.com', 'dashscope.aliyuncs.com', 'platform.vocaligntech.com'):
            entry = dict(host=host, verified=False)
            report['hosts'].append(entry)
            with socket.create_connection((host, 443), timeout=8) as raw:
                with context.wrap_socket(raw, server_hostname=host) as tls:
                    entry.update(verified=True, tls=tls.version(), cipher=tls.cipher()[0],
                                 peer_sha256=hashlib.sha256(tls.getpeercert(binary_form=True)).hexdigest())
        report['complete'] = True
    finally:
        (out/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    print(json.dumps(verify(parser.parse_args().out)))
