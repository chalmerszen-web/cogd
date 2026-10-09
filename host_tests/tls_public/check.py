"""Independent Python pow/OpenSSL-backed RSA vectors against pinned32-bit C."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import subprocess
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import padding,rsa

p=argparse.ArgumentParser()
p.add_argument('--out',type=Path,required=True)
p.add_argument('--binary',required=True,help='WSL absolute test harness path')
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
rng=random.Random(129170)
cases=[]
def add_math(kind,base,exponent,modulus):
    valid=modulus>0 and modulus%2 and exponent>=0
    expected=pow(base,exponent,modulus) if valid else None
    if kind=='C' and valid: expected=pow(expected,exponent,modulus)
    public=kind!='S' and abs(modulus).bit_length()>3072
    count=2 if kind=='C' and valid else 1
    cases.append(dict(line=f'{kind} {base:x} {exponent:x} {modulus:x}',kind='math',
        valid=bool(valid),expected=expected,hardware=0 if public else count,public=count if public else 0))
for bits in (127,1024,2048,3072,3073,4096):
    modulus=(1<<(bits-1))|rng.getrandbits(bits-1)|1
    for exponent in (0,1,3,65537,rng.getrandbits(127)):
        for mode in ('P','S','C'):
            add_math(mode,rng.getrandbits(bits+64)-rng.getrandbits(bits+64),exponent,modulus)
    add_math('P',modulus,65537,modulus)
    add_math('P',0,65537,modulus)
    add_math('P',2,-1,modulus)
    add_math('P',2,65537,-modulus)
    add_math('P',2,65537,modulus+1)
for mode in ('P','S'): add_math(mode,2,65537,0)
digest=hashlib.sha256(b'ESP-HI public RSA path regression').digest()
for bits in (2048,3072,4096):
    # Ephemeral test key: only public material and signatures are saved.
    key=rsa.generate_private_key(public_exponent=65537,key_size=bits)
    public=key.public_key().public_bytes(serialization.Encoding.DER,serialization.PublicFormat.PKCS1)
    for mode,pad in (('V',padding.PKCS1v15()),('W',padding.PSS(mgf=padding.MGF1(hashes.SHA256()),salt_length=32))):
        message=b'ESP-HI public RSA path regression'
        signature=key.sign(message,pad,hashes.SHA256())
        for variant in ('valid','bad_digest','bad_signature'):
            h=digest if variant!='bad_digest' else bytes([digest[0]^1])+digest[1:]
            s=signature if variant!='bad_signature' else signature[:-1]+bytes([signature[-1]^1])
            cases.append(dict(line=f'{mode} {public.hex()} {h.hex()} {s.hex()}',kind='rsa',
                bits=bits,variant=variant,valid=variant=='valid',
                public=1 if bits>3072 else 0,hardware=0 if bits>3072 else 1))
(a.out/'vectors.json').write_text(json.dumps(cases,indent=2),encoding='utf8')
completed=subprocess.run(['wsl','-d','Ubuntu','--',a.binary],input='\n'.join(c['line'] for c in cases)+'\n',
                         text=True,encoding='utf8',errors='replace',capture_output=True)
(a.out/'stdout.jsonl').write_text(completed.stdout,encoding='utf8')
(a.out/'stderr.log').write_text(completed.stderr,encoding='utf8')
assert completed.returncode==0,completed.returncode
rows=[json.loads(s) for s in completed.stdout.splitlines()]
assert len(rows)==len(cases)
for i,(case,row) in enumerate(zip(cases,rows)):
    assert (row['status']==0)==case['valid'],(i,case,row)
    assert row['hardware']==case['hardware'] and row['public']==case['public'],(i,case,row)
    if case['kind']=='math' and case['valid']:
        assert int(row['value'],16)==case['expected'],(i,case,row)
result=dict(passed=len(cases),math=sum(c['kind']=='math' for c in cases),
    rsa=sum(c['kind']=='rsa' for c in cases),limb_bits=32,seed=129170,
    sanitizer=True,private_keys_saved=False,
    vectors_sha256=hashlib.sha256((a.out/'vectors.json').read_bytes()).hexdigest())
(a.out/'report.json').write_text(json.dumps(result,indent=2),encoding='utf8')
print(json.dumps(result))
