"""Read P4's on-device diagnostic journal over USB without resetting the board."""
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0,str(Path(__file__).resolve().parent/'voice'))
from device import Device

ROOT=Path(__file__).resolve().parents[1]
KINDS={1:'sdk',2:'voice',3:'health',4:'boot',5:'marker'}
RESETS={0:'unknown',1:'power_on',2:'external',3:'software',4:'panic',5:'interrupt_watchdog',
        6:'task_watchdog',7:'watchdog',8:'deep_sleep',9:'brownout',10:'sdio',
        11:'usb',12:'jtag',13:'efuse',14:'power_glitch',15:'cpu_lockup'}

def decode_page(data,expected=None):
    if len(data)!=4096:raise ValueError('Incomplete journal page')
    fields=struct.unpack_from('<IHHQIIQQIIIIII',data)
    magic,version,header,seq,boot,used,up,wall,dropped,truncated,reset,flags,reserved,crc=fields
    if (magic,version,header)!=(0x31474c44,1,64) or not seq or not 64<=used<=4092:
        raise ValueError('Invalid journal header')
    if expected is not None and expected!=seq:raise ValueError('Journal sequence changed')
    if struct.unpack_from('<I',data,4092)[0]!=0x43474c44:raise ValueError('Uncommitted page')
    if zlib.crc32(data[64:4092],zlib.crc32(data[:60]))!=crc:raise ValueError('Journal CRC mismatch')
    meta=dict(sequence=seq,boot=boot,uptime_ms=up,unix_ms=wall,dropped=dropped,
              truncated=truncated,reset_reason=reset,reset_name=RESETS.get(reset,'unknown'))
    records=[];pos=64
    while pos<used:
        if used-pos<12:raise ValueError('Truncated record header')
        at,size,kind,record_flags=struct.unpack_from('<QHBB',data,pos);pos+=12
        if size>704 or size>used-pos:raise ValueError('Invalid record length')
        text=data[pos:pos+size].decode('utf8','replace');pos+=size
        timestamp=wall+at-up if wall else None
        local=dt.datetime.fromtimestamp(timestamp/1000,dt.timezone.utc).astimezone().isoformat(timespec='milliseconds') if timestamp else None
        records.append(dict(sequence=seq,boot=boot,uptime_ms=at,unix_ms=timestamp,time=local,
                            kind=KINDS.get(kind,str(kind)),flags=record_flags,text=text))
    return meta,records

def run(args):
    out=args.out or ROOT/'.local/p4-logs'/dt.datetime.now().strftime('%Y%m%d-%H%M%S')
    out.mkdir(parents=True,exist_ok=False)
    d=Device(out/'usb',args.port);d.link.timeout=.015
    result={'started':dt.datetime.now().astimezone().isoformat(),'port':args.port,'complete':False}
    resume=False
    def query(s):
        # Every command has a specific response schema; do not consume unrelated
        # SDK JSON or another component's asynchronous output as a page reply.
        Device.QUERY_FIELDS[s]='log_page' if s.startswith('agent log read ') else 'log'
        return d.command(s,query=True,timeout=8,cancelled_ok=True)
    try:
        result['device']=d.command('agent status',query=True,timeout=8)
        result['before']=query('agent log status')
        if args.action!='export':
            command='agent log '+args.action
            if args.action=='mark':command+=' '+args.text
            result['after']=query(command);result['complete']=True
            print(json.dumps(result['after'],ensure_ascii=False,indent=2))
            return result
        before=result['before']
        if not before.get('ready'):raise RuntimeError('Device journal is unavailable')
        resume=bool(before['enabled'] and not before['paused'])
        frozen=query('agent log freeze');result['snapshot']=frozen
        first,last=frozen['first_sequence'],frozen['last_sequence']
        if args.last_pages:first=max(first,last-args.last_pages+1)
        metas=[];records=[];missing=[]
        pages=out/'pages';pages.mkdir()
        for seq in range(first,last+1) if last else []:
            data=bytearray()
            for offset in range(0,4096,256):
                try:r=query(f'agent log read {seq} {offset} 256')
                except RuntimeError as e:
                    if offset==0 and str(e)=='@error not_found':missing.append(seq);break
                    raise
                if r.get('log_page')!=seq or r.get('offset')!=offset:raise ValueError('USB reply identity mismatch')
                chunk=bytes.fromhex(r['hex'])
                if len(chunk)!=256:raise ValueError('USB chunk size mismatch')
                data.extend(chunk)
            if not data:continue
            meta,rows=decode_page(data,seq)
            (pages/f'{seq:016d}.bin').write_bytes(data)
            meta['sha256']=hashlib.sha256(data).hexdigest();metas.append(meta);records.extend(rows)
            if len(metas)%16==0:print(f'Exported {len(metas)}/{last-first+1} pages',flush=True)
        result.update(pages=metas,missing_sequences=missing,record_count=len(records),
                      complete=not missing,export_pauses_recording=True)
        with (out/'events.jsonl').open('w',encoding='utf8') as f:
            for row in records:f.write(json.dumps(row,ensure_ascii=False)+'\n')
        with (out/'runtime.log').open('w',encoding='utf8') as f:
            f.write('P4 diagnostic journal: SDK and voice events, not a CPU core dump.\n')
            f.write('Page counters disclose dropped/truncated input. A sudden power loss may lose the current batch (nominally 2.5 s, longer if scheduling stalls).\n')
            for meta in metas:
                f.write(f"PAGE {meta['sequence']} boot={meta['boot']:08x} reset={meta['reset_name']} dropped={meta['dropped']} truncated={meta['truncated']}\n")
                for row in (r for r in records if r['sequence']==meta['sequence']):
                    f.write(f"[{row['time'] or 'clock-unset'} boot={row['boot']:08x} +{row['uptime_ms']}ms {row['kind']} flags={row['flags']}] {row['text'].strip()}\n")
        print(f"Saved {len(metas)} CRC-verified pages, {len(records)} records to {out}",flush=True)
        return result
    except Exception as exc:
        result['error']=type(exc).__name__+': '+str(exc)
        raise
    finally:
        if resume:
            try:result['resumed']=query('agent log resume')
            except Exception as e:result['resume_error']=str(e)
        result['finished']=dt.datetime.now().astimezone().isoformat()
        (out/'manifest.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
        d.save();d.close()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['status','on','off','export','mark'],nargs='?',default='export')
    parser.add_argument('--port',default='COM3')
    parser.add_argument('--out',type=Path)
    parser.add_argument('--last-pages',type=int,default=0)
    parser.add_argument('--text',default='manual marker')
    args=parser.parse_args()
    if args.last_pages<0:parser.error('--last-pages must be nonnegative')
    result=run(args)
    return 0 if result['complete'] and 'resume_error' not in result else 1

if __name__=='__main__':raise SystemExit(main())
