"""Check whether an ESP rebuild changes only its embedded ELF identity/digests.

Both images must have valid ESP segment checksums and appended SHA-256. Every
byte outside the 32-byte ELF identity, checksum byte and image digest must match.
This does not establish runtime acceptance of a different executable image.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from migrate_context import application_size


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('before',type=Path);p.add_argument('after',type=Path)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    assert not args.output.exists()
    before=args.before.read_bytes();after=args.after.read_bytes()
    old_size=application_size(before);new_size=application_size(after)
    assert len(before)==old_size and len(after)==new_size
    # Header (24), first segment header (8), then esp_app_desc_t (magic,
    # secure/reserved, version/project/time/date/idf-version, app_elf_sha256).
    assert struct.unpack_from('<I',before,32)[0]==struct.unpack_from('<I',after,32)[0]==0xabcd5432
    differences=[i for i,(a,b) in enumerate(zip(before,after)) if a!=b]
    unexpected=[i for i in differences if not (176<=i<208 or len(before)-33<=i<len(before))]
    passed=len(before)==len(after) and not unexpected
    report=dict(method=__doc__,before=str(args.before),after=str(args.after),
        before_sha256=hashlib.sha256(before).hexdigest(),after_sha256=hashlib.sha256(after).hexdigest(),
        before_bytes=len(before),after_bytes=len(after),differences=differences,unexpected_differences=unexpected,
        all_executable_and_other_data_equal=passed)
    args.output.write_text(json.dumps(report,indent=2),encoding='utf8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('differences','unexpected_differences')}))
    assert passed


if __name__=='__main__':
    main()
