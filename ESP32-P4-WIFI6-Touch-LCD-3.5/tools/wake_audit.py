"""Screen short known-language sources with 500 ms recognizer right context.

Padding is supplied only to ASR, never saved into the playback/training WAV.
Language is the synthesis source language, not a prompted target transcript.
"""
import argparse,json
from pathlib import Path
import numpy as np
import soundfile as sf
import sherpa_onnx
ROOT=Path(__file__).resolve().parents[2]
def main():
    p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    decoders={};base=ROOT/'artifacts/kws-phase2/asr';clips=[]
    for row in map(json.loads,a.manifest.read_text(encoding='utf8').splitlines()):
        lang=row['language']
        if lang not in decoders:decoders[lang]=sherpa_onnx.OfflineRecognizer.from_sense_voice(model=str(base/'model.int8.onnx'),tokens=str(base/'tokens.txt'),num_threads=4,use_itn=True,language=lang)
        samples,rate=sf.read(ROOT/row['path'],dtype='float32');decoder=decoders[lang];s=decoder.create_stream();s.accept_waveform(rate,np.r_[samples,np.zeros(rate//2,dtype=np.float32)]);decoder.decode_stream(s)
        clips.append({'clip_id':row['clip_id'],'asr_text':s.result.text,'asr_language':s.result.lang,'source_language':lang,'padding_ms':500})
    a.out.write_text(json.dumps({'method':'SenseVoice with known source language and 500 ms right context; no target text prompt; automatic only','clips':clips},ensure_ascii=False,indent=2),encoding='utf8')
    print('Screened',len(clips),flush=True)
if __name__=='__main__':main()
