"""Stream frozen PCM into the temporary P4 MultiNet probe over UART."""
import argparse,hashlib,json,time,wave
from pathlib import Path
import numpy as np
import serial
ROOT=Path(__file__).resolve().parents[2]

def fnv1a(data):
    value=2166136261
    for byte in data:value=((value^byte)*16777619)&0xffffffff
    return value

def main():
    p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True);p.add_argument('--app',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--gain',type=int,choices=(1,4),default=1);p.add_argument('--threshold',type=float,default=.5);p.add_argument('--limit',type=int);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    manifest=json.loads(a.manifest.read_text());trials=manifest['trials'][:a.limit];assert trials and all(t['split'] in ('train','validation') for t in trials)
    port=serial.Serial();port.port='COM3';port.baudrate=921600;port.timeout=.2;port.write_timeout=5;port.dtr=False;port.rts=False;port.open()
    log=open(a.out/'serial.log','w',encoding='utf8');report={'complete':False,'scope':manifest['scope'],'gain':a.gain,'threshold':a.threshold,'trials':[],
        'manifest_sha256':hashlib.sha256(a.manifest.read_bytes()).hexdigest(),'app_sha256':hashlib.sha256(a.app.read_bytes()).hexdigest()}
    def save():(a.out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    def receive(kind,timeout=40):
        until=time.monotonic()+timeout
        while time.monotonic()<until:
            raw=port.readline()
            if not raw:continue
            line=raw.decode('utf8','replace').strip();log.write(line+'\n');log.flush()
            if any(s in line for s in ('Guru Meditation','abort()','Rebooting...')):
                tail_until=time.monotonic()+2
                while time.monotonic()<tail_until:
                    log.write(port.readline().decode('utf8','replace'));log.flush()
                raise RuntimeError('Probe reboot/panic; inspect serial.log')
            try:value=json.loads(line)
            except json.JSONDecodeError:continue
            if value.get('type') in ('fatal','error'):raise RuntimeError(str(value))
            if value.get('type')==kind:return value
        raise TimeoutError('Waiting for '+kind)
    try:
        port.write(b'PING\n');report['device']=receive('pong',15);assert report['device']['cpu_mhz']==200
        for t in trials:
            report['active_trial']=t['id'];report['active_frame']=-1;save()
            path=ROOT/t['path'];assert hashlib.sha256(path.read_bytes()).hexdigest()==t['sha256']
            with wave.open(str(path),'rb') as w:
                assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(1,2,16000);pcm=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(np.int32)
            assert len(pcm)==t['samples'] and len(pcm)%512==0
            pcm*=a.gain;clipped=int(np.sum((pcm>32767)|(pcm<-32768)));pcm=np.pad(np.clip(pcm,-32768,32767).astype('<i2'),(32768,0))
            count=len(pcm)//512;port.write(f'BEGIN {count} {a.threshold:.4f}\n'.encode());begin=receive('begin');assert begin['frames']==count and begin['chunk']==512
            frames=[];accepted=[];cooldown=0
            for n,block in enumerate(pcm.reshape(-1,512)):
                report['active_frame']=n;payload=block.tobytes()
                port.write(payload);row=receive('frame',8);assert row['frame']==n and row['pcm_fnv1a']==fnv1a(payload);frames.append(row)
                end=(n+1)*512-32768
                if n>=64 and row['state']==1 and end>=cooldown:
                    accepted.append({'sample_end':end,'matches':row.get('matches',[]),'raw':row.get('raw','')});cooldown=end+24000
            end=receive('end');assert not end['failed'];lo,hi=t['accept_bounds']
            hit=bool(t['label']) and any(lo<=e['sample_end']<=hi for e in accepted);false=any(not t['label'] or not lo<=e['sample_end']<=hi for e in accepted)
            (a.out/(t['id']+'.jsonl')).write_text(''.join(json.dumps(r)+'\n' for r in frames),encoding='utf8')
            report['trials'].append(dict(t,begin=begin,end=end,events=accepted,hit=hit,false=false,clipped_samples=clipped));save()
            print(json.dumps({'id':t['id'],'hit':hit,'false':false,'max_us':end['max_us'],'events':accepted}),flush=True)
        groups={}
        for t in report['trials']:
            g=groups.setdefault(t['group'],{'count':0,'hits':0,'false':0});g['count']+=1;g['hits']+=t['hit'];g['false']+=t['false']
        report.update(complete=True,groups=groups,active_trial=None,active_frame=None);print(json.dumps({'groups':groups}),flush=True)
    except Exception as e:report['error']=str(e);raise
    finally:save();log.close();port.close()

if __name__=='__main__':main()
