"""Replay saved PCM on the USB-only C3 probe; never record or call a provider."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import time
import wave
import zlib

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from serial_link import connect

VERSION='0.11.123-vad-replay'
FRAME_BYTES=640

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def check_probe(state,manifest):
    expected=manifest.get('firmware',VERSION);mode=manifest.get('vad_mode',3)
    if not isinstance(expected,str) or type(mode) is not int or mode not in (2,3):
        raise ValueError('Invalid declared probe version or VAD mode')
    if state.get('version')!=expected:
        raise ValueError('Device probe version does not match manifest')
    # Frozen123 predates this field; it always creates MODE_3. No inference for
    # other versions, so a candidate cannot be labelled as baseline accidentally.
    actual=state.get('vad_mode',3 if expected==VERSION else None)
    if type(actual) is not int or actual!=mode:
        raise ValueError('Device VAD mode does not match manifest')
    filtering=manifest.get('input_filter','production')
    if filtering not in ('production','clean') or state.get('input_filter','production')!=filtering:
        raise ValueError('Device VAD input filter does not match manifest')
    return mode


def same_energy(expected,actual):
    """Only the spectral bit may differ in an input-filter comparison."""
    if not expected or len(expected)!=len(actual) or len(expected)%6:
        return False
    return all(expected[i:i+4]==actual[i:i+4] and
               expected[i+4] in (0,1) and actual[i+4] in (0,1) and
               expected[i+5]==actual[i+5]==0xa6 for i in range(0,len(expected),6))

def frame_command(index,pcm):
    if type(index) is not int or not 0<=index<500 or len(pcm)!=FRAME_BYTES:
        raise ValueError('Invalid frame input')
    return f'frame {index} {pcm.hex()} {zlib.crc32(pcm):08x}'

def metadata(reply,index):
    if type(reply.get('frame')) is not int or reply['frame']!=index:
        raise ValueError('Out-of-order frame response')
    values=[reply.get('level'),reply.get('clean')]
    if any(type(v) is not int or not 0<=v<=32768 for v in values) or type(reply.get('spectral')) is not bool:
        raise ValueError('Invalid metadata values')
    return struct.pack('<HHBB',*values,int(reply['spectral']),0xa6)

def load_wav(path):
    with wave.open(str(path),'rb') as f:
        if (f.getnchannels(),f.getsampwidth(),f.getframerate(),f.getcomptype())!=(1,2,16000,'NONE'):
            raise ValueError('Probe requires raw 16-kHz mono16 PCM')
        n=f.getnframes()
        if not 320<=n<=160000:raise ValueError('Clip exceeds fixed capture bound')
        pcm=f.readframes(n)
    if len(pcm)!=n*2:raise ValueError('Incomplete WAV')
    tail=n%320
    return pcm[:(n-tail)*2],tail

class Probe:
    def __init__(self,port,out):
        self.link=connect(port);self.link.timeout=.05
        self.log=(out/'serial.log').open('wb');self.deadline=time.monotonic()+180
    def receive(self,timeout=2):
        deadline=min(self.deadline,time.monotonic()+timeout)
        data=b''
        while time.monotonic()<deadline:
            part=self.link.readline();self.log.write(part);self.log.flush();data+=part
            if len(data)>65536:raise ValueError('Unbounded probe response')
            while b'\n' in data:
                line,data=data.split(b'\n',1)
                if line.startswith(b'@vadprobe '):return json.loads(line[10:])
        raise TimeoutError('Probe response not received within observation budget')
    def command(self,text,*,error=None):
        if time.monotonic()>self.deadline:raise TimeoutError('Probe session deadline')
        self.link.write((text+'\n').encode('ascii'))
        reply=self.receive()
        if reply.get('error')!=error:raise ValueError(f'Unexpected probe error: {reply.get("error")}')
        return reply
    def close(self):self.link.close();self.log.close()

def protocol_checks(p):
    zero=b'\0'*FRAME_BYTES;checks=[]
    def bad(command,code):
        result=p.command(command,error=code)
        assert not p.command('status')['active']
        checks.append(dict(error=code,passed=True))
        return result
    bad('begin 0','begin');bad('begin 501','begin');bad('end','incomplete')
    p.command('begin 1');bad(frame_command(1,zero),'sequence')
    p.command('begin 1');bad(frame_command(0,zero)[:-8]+'00000000','crc')
    p.command('begin 1');bad('frame 0 g'+zero.hex()[1:]+' '+f'{zlib.crc32(zero):08x}','hex')
    p.command('begin 1');bad('x'*1400,'line_limit')
    p.command('begin 2');assert metadata(p.command(frame_command(0,zero)),0)==b'\0\0\0\0\0\xa6'
    bad('end','incomplete')
    p.command('begin 1');p.command(frame_command(0,zero));bad(frame_command(0,zero),'sequence')
    p.command('begin 500');started=time.monotonic();assert p.command('abort')['aborted']
    checks.append(dict(abort_ms=round((time.monotonic()-started)*1000,3),passed=True))
    p.command('begin 1');timeout=p.receive(timeout=12)
    assert timeout.get('error')=='idle_timeout' and not p.command('status')['active']
    checks.append(dict(idle_timeout=True,passed=True))
    p.command('begin 500');records=b''
    for i in range(500):records+=metadata(p.command(frame_command(i,zero)),i)
    done=p.command('end')
    assert done['frames']==500 and done['pcm_crc32']==f'{zlib.crc32(zero*500):08x}'
    assert done['metadata_crc32']==f'{zlib.crc32(records):08x}'
    checks.append(dict(max_frames=500,passed=True))
    return checks

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--port',default='COM5')
    parser.add_argument('--verify-protocol',action='store_true')
    args=parser.parse_args();args.out=args.out.resolve()
    if not args.out.is_relative_to(ROOT):parser.error('--out must be inside this workspace')
    args.out.mkdir(parents=True,exist_ok=False)
    manifest=json.loads(args.manifest.read_text(encoding='utf8'));inputs=manifest['clips']
    if not 1<=len(inputs)<=8:raise ValueError('Bounded one-to-eight clip manifest required')
    report=dict(complete=False,rows=[],started=time.time(),manifest_sha256=sha(args.manifest),script_sha256=sha(Path(__file__)),
        acquisition=False,network_requests=0,synthetic_reconstruction=False)
    p=Probe(args.port,args.out)
    try:
        state=p.command('status');report['before']=state
        report['vad_mode']=check_probe(state,manifest)
        assert state['vendor_sha256']==sha(ROOT/'third_party/esp-sr/lib/esp32c3/libesp_audio_processor.a')
        if args.verify_protocol:report['protocol']=protocol_checks(p)
        for index,item in enumerate(inputs):
            path=(ROOT/item['wav']).resolve();assert path.is_relative_to(ROOT)
            assert sha(path)==item['wav_sha256']
            pcm,tail=load_wav(path);count=len(pcm)//FRAME_BYTES
            assert p.command(f'begin {count}')==dict(begin=count)
            records=bytearray();frames=[]
            for i in range(count):
                row=p.command(frame_command(i,pcm[i*FRAME_BYTES:(i+1)*FRAME_BYTES]))
                records.extend(metadata(row,i));frames.append(row)
            done=p.command('end')
            assert done['frames']==count and done['pcm_crc32']==f'{zlib.crc32(pcm):08x}' and done['metadata_crc32']==f'{zlib.crc32(records):08x}'
            binary=args.out/f'clip-{index+1:02}.bin';binary.write_bytes(records)
            (args.out/f'clip-{index+1:02}.json').write_text(json.dumps(frames,indent=2)+'\n')
            row=dict(id=item['id'],wav=str(path.relative_to(ROOT)),wav_sha256=sha(path),frames=count,discarded_partial_frame_samples=tail,
                metadata=str(binary.relative_to(ROOT)),metadata_sha256=sha(binary),crc=done,max_us=max(x['us'] for x in frames))
            report['rows'].append(row)
            if 'expected' in item:
                expected=ROOT/item['expected'];assert sha(expected)==item['expected_sha256']
                row['parity']=expected.read_bytes()==records
                if not row['parity']:
                    row['different_frames']=[i for i in range(count) if expected.read_bytes()[i*6:(i+1)*6]!=records[i*6:(i+1)*6]]
                    raise ValueError('Vendor/filter parity failed; new attribution refused')
            if 'expected_energy' in item:
                expected=ROOT/item['expected_energy'];assert sha(expected)==item['expected_energy_sha256']
                row['energy_parity']=same_energy(expected.read_bytes(),records)
                if not row['energy_parity']:
                    raise ValueError('Energy parity failed; filter comparison refused')
            print(json.dumps(dict(id=row['id'],frames=count,parity=row.get('parity'),max_us=row['max_us'])),flush=True)
        report['after']=p.command('status');assert not report['after']['active']
        report['complete']=True
    except Exception as error:
        report['error']=type(error).__name__+': '+str(error);raise
    finally:
        try:p.command('abort')
        except Exception as error:report['cleanup_error']=str(error)
        p.close();report['ended']=time.time()
        (args.out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

if __name__=='__main__':main()
