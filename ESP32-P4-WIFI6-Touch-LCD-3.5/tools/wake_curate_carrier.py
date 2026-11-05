"""Conservative carrier extraction, followed by an independent crop ASR pass."""
import argparse,hashlib,json,sys
from pathlib import Path
import numpy as np
import soundfile as sf
import sherpa_onnx
from wake_prepare import ROOT,clean,screened,bounds

def main():
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out=a.out.resolve();a.out.mkdir(parents=True,exist_ok=False)
    base=ROOT/'artifacts/kws-phase2/asr'
    decoder=sherpa_onnx.OfflineRecognizer.from_sense_voice(model=str(base/'model.int8.onnx'),tokens=str(base/'tokens.txt'),num_threads=4,use_itn=True,language='yue')
    def recognize(signal):
        stream=decoder.create_stream();stream.accept_waveform(16000,np.r_[signal,np.zeros(8000,dtype=np.float32)]);decoder.decode_stream(stream);return stream.result
    accepted=[];audit=[]
    for row in map(json.loads,a.source.read_text(encoding='utf8').splitlines()):
        signal,rate=sf.read(ROOT/row['path'],dtype='float32');assert rate==16000
        result=recognize(signal);note={'clip_id':row['clip_id'],'full_text':result.text,'reason':None}
        chars=[(c,float(t)) for token,t in zip(result.tokens,result.timestamps) for c in clean(token)]
        marker=next((i for i,(c,t) in enumerate(chars) if c=='今'),None)
        if marker is None or marker<1:note['reason']='carrier_not_located'
        elif not screened(row,''.join(c for c,t in chars[:marker])):note['reason']='prefix_not_confirmed'
        else:
            start=max(0,int((chars[0][1]-.20)*16000))
            lo=int((chars[marker-1][1]+.18)*16000);hi=min(len(signal)-160,int((chars[marker][1]-.10)*16000))
            if hi<=lo+320:note['reason']='no_safe_boundary'
            else:
                positions=np.arange(lo,hi,80);energy=np.array([np.mean(signal[pos:pos+160]**2) for pos in positions])
                end=int(positions[np.argmin(energy)])+160;crop=signal[start:end]
                result2=recognize(crop);note.update(asr_text=result2.text,asr_language=result2.lang,crop=[start,end])
                raw=np.clip(np.rint(crop*32768),-32768,32767).astype('<i2')
                if not screened(row,result2.text):note['reason']='crop_not_confirmed'
                else:
                    try:low,high=bounds(raw)
                    except ValueError as e:note['reason']=str(e)
                if note['reason'] is None:
                    path=a.out/(row['clip_id']+'.wav');sf.write(path,raw,16000,subtype='PCM_16')
                    accepted.append(dict(row,path=path.relative_to(ROOT).as_posix(),carrier=False,source_carrier_path=row['path'],
                        source_pcm_sha256=row['pcm_sha256'],pcm_sha256=hashlib.sha256(raw.tobytes()).hexdigest(),
                        wav_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),wake_start_sample=low,wake_end_sample=high,
                        seconds=len(raw)/16000,crop_start=start,crop_end=end,annotation='ASR timestamps, energy trough, isolated crop ASR; automatic screening only'))
        audit.append(note)
        print(json.dumps({'id':row['clip_id'],'accepted':note['reason'] is None,'text':note.get('asr_text',note['full_text']),'reason':note['reason']},ensure_ascii=False),flush=True)
    (a.out/'manifest.jsonl').write_text(''.join(json.dumps(r,ensure_ascii=False)+'\n' for r in accepted),encoding='utf8')
    (a.out/'audit.json').write_text(json.dumps({'complete':True,'accepted':len(accepted),'total':len(audit),'clips':audit},ensure_ascii=False,indent=2),encoding='utf8')

if __name__=='__main__':main()
