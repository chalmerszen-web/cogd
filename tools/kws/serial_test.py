"""Bounded board parity and real-microphone performance tests; no recognition claim."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import time
import zlib

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from serial_link import connect

class Link:
    def __init__(self,port,log): self.link=connect(port);self.log=log
    def close(self): self.link.close()
    def command(self,text,expected_error=None,timeout=15):
        # All commands here are diagnostics and contain no credentials.
        self.link.write((text+'\n').encode())
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            line=self.link.readline().decode('utf8','replace').strip()
            if not line: continue
            with self.log.open('a',encoding='utf8') as f: f.write(line+'\n')
            if line.startswith('@error'):
                if expected_error and expected_error in line: return line
                raise RuntimeError(line)
            if line.startswith('@ok'):
                assert expected_error is None,(text,'error expected');return line
            if line.startswith('{'):
                try: obj=json.loads(line)
                except ValueError: continue
                assert expected_error is None,(text,'error expected');return obj
        raise TimeoutError(text[:40])
    def off(self):
        self.command('agent wake off')
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            s=self.command('agent wake status')
            if not s['enabled'] and s['chunk']==0:return s
            time.sleep(.1)
        raise TimeoutError('wake off')

def parity(link,out,channels=24):
    traces=json.loads((out/'fixed-traces.json').read_text());pcm=(out/'fixed.pcm').read_bytes()
    fusion=json.loads((out/'fusion-traces.json').read_text()) if (out/'fusion-traces.json').exists() else None
    verified=json.loads((out/'verified-traces.json').read_text()) if (out/'verified-traces.json').exists() else None
    values=11*channels+1
    assert channels in (24,48) and all(len(t['layer'])==values for t in traces)
    report=dict(complete=False,frames=0,mismatches=[],max_us=0,frame_us=[],channels=channels,started=time.time())
    report['pcm_sha256']=hashlib.sha256(pcm).hexdigest()
    report['trace_sha256']=hashlib.sha256((out/'fixed-traces.json').read_bytes()).hexdigest()
    if fusion is not None:
        assert len(fusion)==len(traces)
        report['fusion_trace_sha256']=hashlib.sha256((out/'fusion-traces.json').read_bytes()).hexdigest()
    if verified is not None:
        assert len(verified)==len(traces) and channels==48
        report['verified_trace_sha256']=hashlib.sha256((out/'verified-traces.json').read_bytes()).hexdigest()
    if (out/'host-parity.json').exists():
        report['host_reference']=json.loads((out/'host-parity.json').read_text())
        assert report['host_reference']['pcm_sha256']==report['pcm_sha256']
        if 'trace_sha256' in report['host_reference']:
            assert report['host_reference']['trace_sha256']==report['trace_sha256']
    link.off();link.command('agent kws begin')
    try:
        frame=pcm[:512];crc=zlib.crc32(frame);hexadecimal=frame.hex()
        link.command(f'agent kws frame 1 {crc:x} {hexadecimal}',expected_error='argument')
        link.command(f'agent kws frame 0 {crc^1:x} {hexadecimal}',expected_error='argument')
        link.command('agent wake on',expected_error='busy')
        for i,trace in enumerate(traces):
            frame=pcm[i*512:(i+1)*512];crc=zlib.crc32(frame)
            reply=link.command(f'agent kws frame {i} {crc:x} {frame.hex()}')
            raw=struct.pack(f'<40h40b{values}h',*trace['logmel'],*trace['input'],*trace['layer'])
            assert reply['seq']==i and reply['crc']==crc
            if bytes.fromhex(reply['trace'])!=raw:
                report['mismatches'].append(i)
                (out/f'mismatch-{i}.json').write_text(json.dumps(dict(expected=trace,actual=reply)))
                raise AssertionError(f'Board mismatch at frame {i}')
            if fusion is not None:
                expected=fusion[i]['members']+[fusion[i]['score'],fusion[i]['detected']]
                assert reply.get('fusion')==expected,(i,reply.get('fusion'),expected)
            if verified is not None:
                assert reply.get('verified')==verified[i],(i,reply.get('verified'),verified[i])
            report['frames']+=1;report['max_us']=max(report['max_us'],reply['us']);report['frame_us'].append(reply['us'])
            if i%64==0: print(f'board parity {i+1}/{len(traces)}',flush=True)
        link.command('agent cancel')
        link.command('agent kws frame 0 0 00',expected_error='argument')
        link.command('agent kws begin');link.command('agent kws end')
        assert len(report['frame_us'])%2==0
        report.update(complete=True,values=report['frames']*(40+40+values),ended=time.time(),
            max_512_pair_us=max(sum(report['frame_us'][i:i+2]) for i in range(0,len(report['frame_us']),2)))
    finally:
        link.command('agent kws end')
        (out/'board-parity.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report),flush=True)

def benchmark(link,out,seconds):
    link.off();link.command('agent kws reset-profile')
    report=dict(complete=False,seconds=seconds,started=time.time(),samples=[])
    report['before']=link.command('agent status');report['wake_before']=link.command('agent wake status')
    link.command('agent wake on')
    try:
        start=time.monotonic();next_sample=start
        while time.monotonic()-start<seconds:
            if time.monotonic()>=next_sample:
                row=dict(elapsed=time.monotonic()-start,status=link.command('agent status'),wake=link.command('agent wake status'),profile=link.command('agent kws profile'))
                report['samples'].append(row);next_sample+=30
                (out/'microphone-benchmark.json').write_text(json.dumps(report,indent=2))
                print(json.dumps(dict(elapsed=round(row['elapsed']),blocks=row['profile']['blocks'],max_us=row['profile']['max_us'],free_heap=row['status']['free_heap'])),flush=True)
            time.sleep(.2)
        link.off()
        report['profile']=profile=link.command('agent kws profile')
        report['after']=link.command('agent status');report['wake_after']=link.command('agent wake status')
        count=profile['blocks'];threshold=(count*99+99)//100;cumulative=0
        p99=None
        for i,value in enumerate(profile['histogram']):
            cumulative+=value
            if p99 is None and cumulative>=threshold:p99=(i+1)*500
        assert cumulative==count and count>=(seconds-5)*16000/512
        assert profile['max_us']<32000 and p99<=16000
        assert report['wake_after']['dma_lost']==report['wake_before']['dma_lost']
        assert report['wake_after']['wakes']==report['wake_before']['wakes']
        rows=[r['status'] for r in report['samples'][1:]]
        assert rows and all(r['min_heap']>=32768 and r['largest_block']>=24576 for r in rows)
        report.update(complete=True,p99_us_upper_bound=p99,ended=time.time())
    finally:
        link.off();(out/'microphone-benchmark.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(dict(complete=report['complete'],blocks=count,max_us=profile['max_us'],p99_upper=p99)),flush=True)

def main():
    p=argparse.ArgumentParser();p.add_argument('mode',choices=['parity','benchmark','all']);p.add_argument('--port',default='COM5');p.add_argument('--out',default='artifacts/kws-phase1');p.add_argument('--seconds',type=int,default=600)
    p.add_argument('--channels',type=int,choices=(24,48),default=24)
    args=p.parse_args();out=ROOT/args.out;out.mkdir(parents=True,exist_ok=True)
    link=Link(args.port,out/(args.mode+'-serial.log'))
    try:
        if args.mode in ('parity','all'):parity(link,out,args.channels)
        if args.mode in ('benchmark','all'):benchmark(link,out,args.seconds)
    finally:link.close()

if __name__=='__main__':main()
