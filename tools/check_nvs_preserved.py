"""Compare active NVS records in saved Flash images without exposing values.

Only the agent's persistent sequence reservation may advance during real chats.
The pinned ESP-IDF parser validates headers and variable-length record CRCs.
No USB access, provisioning, repair or Flash writes are performed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys


def active(image):
    from nvs_parser import NVS_Partition
    assert len(image)==0x400000, 'Expected a complete 4-MiB Flash backup'
    partition=NVS_Partition('nvs',bytearray(image[0x9000:0xf000]))
    entries={}
    namespaces={}
    for page in partition.pages:
        if page.is_empty: continue
        assert page.header['status'] in ('Active','Full'), 'Unexpected NVS page state'
        assert page.header['crc']['original']==page.header['crc']['computed'], 'NVS page CRC'
        for entry in page.entries:
            if entry.state!='Written': continue
            meta=entry.metadata; crc=meta['crc']
            assert crc['original']==crc['computed'], 'NVS entry CRC'
            if entry.children:
                assert all(c.state=='Written' for c in entry.children), 'Incomplete NVS value'
                assert crc['data_original']==crc['data_computed'], 'NVS data CRC'
            identity=(meta['namespace'],entry.key,meta['type'],meta['chunk_index'])
            assert identity not in entries, 'Ambiguous duplicate active NVS record'
            entries[identity]=bytes(entry.raw)+b''.join(bytes(c.raw) for c in entry.children)
            if meta['namespace']==0:
                namespaces[entry.data['value']]=entry.key
    agent=next(n for n,name in namespaces.items() if name=='agent_m0')
    sequence=(agent,'seq_hwm','uint64_t',255)
    assert sequence in entries, 'Missing persistent sequence reservation'
    high=int.from_bytes(entries.pop(sequence)[24:32],'little')
    return entries,high


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('before',type=Path);p.add_argument('after',type=Path)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    if args.output.exists():p.error('Use a new evidence path')
    root=Path(__file__).resolve().parents[1]
    sys.path.insert(0,str(root/'.toolchains/esp-idf/components/nvs_flash/nvs_partition_tool'))
    before,after=args.before.read_bytes(),args.after.read_bytes()
    old,old_high=active(before);new,new_high=active(after)
    same=old==new
    report=dict(before=str(args.before),after=str(args.after),
        before_sha256=hashlib.sha256(before).hexdigest(),after_sha256=hashlib.sha256(after).hexdigest(),
        protected_active_records=len(old),protected_records_unchanged=same,
        sequence_before=old_high,sequence_after=new_high,
        sequence_reservation_valid=new_high>=old_high and (new_high-old_high)%128==0,
        scope=__doc__)
    report['passed']=same and report['sequence_reservation_valid']
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2),encoding='utf8')
    print(json.dumps(report));assert report['passed'], 'Protected NVS content changed'


if __name__=='__main__':main()
