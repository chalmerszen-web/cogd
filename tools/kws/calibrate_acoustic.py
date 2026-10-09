"""Two development utterances through the physical board input, no model tuning."""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import sys
import time
import wave

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tools/kws')]
from acoustic_test import playback_source,save


def export_clip(link,path):
    state=link.command('agent audio status')
    if not state['clip_ready']:return None
    samples=state['clip_ms']*16
    data=bytearray()
    for offset in range(0,samples,256):
        count=min(256,samples-offset)
        reply=link.command(f'agent audio clip read {offset} {count}')
        assert reply['offset']==offset
        block=bytes.fromhex(reply['pcm']);assert len(block)==count*2
        data.extend(block)
    with wave.open(str(path),'wb') as f:
        f.setparams((1,2,16000,0,'NONE','not compressed'));f.writeframes(data)
    pcm=array.array('h',data)
    return dict(samples=samples,sha256=hashlib.sha256(data).hexdigest(),
                rms=math.sqrt(sum(x*x for x in pcm)/max(1,len(pcm))),
                peak=max((abs(x) for x in pcm),default=0),
                full_scale_samples=sum(abs(x)>=32767 for x in pcm))


def main():
    from serial_test import Link
    from wave_play import play
    from record_speaker import Recorder
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True)
    p.add_argument('--gain',type=float,default=.35);p.add_argument('--source-rms',type=float,default=0)
    p.add_argument('--device',type=int,default=0);args=p.parse_args()
    if not 0<args.gain<=.5 or not 0<=args.source_rms<=.14:raise ValueError('Level budget')
    args.out.mkdir(parents=True,exist_ok=False)
    rows=[json.loads(s) for s in (ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl').read_text(encoding='utf8').splitlines()]
    chosen=[next(r for r in rows if r['split']=='train' and r['label']==1 and r['language']==lang
                 and 1<=r['seconds']<=4) for lang in ('zh','yue')]
    report=dict(complete=False,selection=chosen,trials=[],gain=args.gain,device=args.device)
    link=Link('COM5',args.out/'serial.log')
    try:
        report['identity']=link.command('agent status');link.off()
        report['previous_clip']=export_clip(link,args.out/'previous-clip.wav')
        for row in chosen:
            source=playback_source(row,args,report)
            lang=row['language'];wav=args.out/f'device-{lang}.wav'
            with Recorder(args.out/f'pc-{lang}.wav',seconds=25,raw=True):
                link.command('agent audio capture 8000')
                deadline=time.monotonic()+12
                while time.monotonic()<deadline:
                    ready=link.command('agent audio status')
                    if ready['recording'] and ready['capture_stage']==2:break
                    time.sleep(.05)
                else:raise TimeoutError('ADC did not start; no audio submitted')
                time.sleep(.5)
                output=play(source,args.device,args.gain)
                deadline=time.monotonic()+10
                while time.monotonic()<deadline:
                    state=link.command('agent audio status')
                    if not state['recording'] and state['clip_ready']:break
                    time.sleep(.1)
                else:raise TimeoutError('capture')
            assert state['capture_error']=='ok' and state['clip_ms']==8000
            report['trials'].append(dict(language=lang,output=output,ready=ready,state=state,signal=export_clip(link,wav)))
            with (args.out/f'manifest-{lang}.jsonl').open('w',encoding='utf8') as f:
                for path in (wav,args.out/f'pc-{lang}.wav'):
                    f.write(json.dumps(dict(clip_id=path.stem,path=path.as_posix(),language=lang,text='你好，小言'),ensure_ascii=False)+'\n')
            save(args.out/'report.json',report)
        report['complete']=True
    finally:
        link.close();save(args.out/'report.json',report)
    print(json.dumps(report,ensure_ascii=False))


if __name__=='__main__':main()
