"""Configure a fake target against real pinned SDK text; no crypto replacement."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser()
p.add_argument('--sdk',type=Path,required=True)
args=p.parse_args()
root=Path(__file__).resolve().parents[1]
source=args.sdk/'components/mbedtls/mbedtls/library/ssl_tls.c'
original=source.read_text(encoding='utf8')
original_bytes=source.read_bytes()
normalized_hash=hashlib.sha256(original.encode()).hexdigest()
assert normalized_hash=='1beac5da2dd0f99f626c81651e9cecb921eb9725a9f17624851802b8c6259ddc'
needle='        ret = mbedtls_ssl_handshake_step(ssl);'
before='        agent_tls_cooperate_step(0);\n'
after='\n        agent_tls_cooperate_step(1);'
with tempfile.TemporaryDirectory(prefix='cogd-tls-cooperate-') as directory:
    base=Path(directory)
    for name,text,passes in (('original',original,True),
                             ('crlf',original.replace('\n','\r\n'),True),
                             ('mutated',original+'\n/* changed SDK */\n',False)):
        case=base/name;case.mkdir()
        (case/'ssl_tls.c').write_bytes(text.encode())
        (case/'dummy.c').write_text('int fixture;\n')
        (case/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.22)\nproject(cooperate C)\n'
            'add_library(fixture STATIC ssl_tls.c dummy.c)\n'
            f'include("{(root/"cmake/tls_cooperate.cmake").as_posix()}")\n'
            'agent_configure_tls_cooperation(fixture)\n')
        run=subprocess.run(['cmake','-S',str(case),'-B',str(case/'build')],capture_output=True,text=True)
        assert (run.returncode==0)==passes,(name,run.stdout,run.stderr)
        generated=case/'build/agent_tls_cooperate_sdk.c'
        if passes:
            adapted=generated.read_text()
            prefix='extern void agent_tls_cooperate_step(int finished);\n'
            assert adapted.startswith(prefix)
            restored=adapted[len(prefix):].replace(before+needle+after,needle)
            assert restored==original and adapted.count(before)==adapted.count(after)==1
        else:
            assert 'Review the pinned TLS outer loop' in run.stderr and not generated.exists()
assert source.read_bytes()==original_bytes
print('TLS adapter exact outer-loop insertion, CRLF normalization, modified-SDK rejection and SDK unchanged OK')
