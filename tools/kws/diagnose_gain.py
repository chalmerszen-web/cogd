"""Train-source recordings only: diagnose level sensitivity without threshold tuning."""
import json
from pathlib import Path
import sys
import wave
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/kws'),str(ROOT/'training/kws')]
from parity import Backend
from evaluate import events


def main():
    out=ROOT/'artifacts/kws-phase5';out.mkdir(exist_ok=True)
    backend=Backend(ROOT/'build-kws-phase3-host/libkws.so','kws_trained_model')
    rows=[]
    for lang in ('zh','yue'):
        path=ROOT/f'artifacts/kws-phase4/calibration-final/device-{lang}.wav'
        with wave.open(str(path),'rb') as f:raw=np.frombuffer(f.readframes(f.getnframes()),dtype='<i2')
        for gain in (.25,.5,1,2,4,8,16):
            value=raw.astype(np.float64)*gain
            pcm=np.pad(np.clip(np.rint(value),-32768,32767).astype(np.int16),(35840,16000))
            pcm=np.pad(pcm,(0,(-len(pcm))%512))
            backend.lib.kws_reset(backend.handle)
            scores=np.array([backend.pcm(frame).layer[-1] for frame in pcm.reshape(-1,256)])
            # The calibration utterance began 0.5s after the ADC became ready.
            begin=(35840+6400)//256;end=(35840+48000)//256
            detected=events(scores,625)
            rows.append(dict(language=lang,gain=gain,max_during_speech=int(scores[begin:end].max()),
                events_seconds=[(x-35840)/16000 for x in detected],clipped_samples=int((np.abs(value)>32767).sum())))
    (out/'gain-diagnosis.json').write_text(json.dumps(rows,indent=2),encoding='utf8')
    print(json.dumps(rows))


if __name__=='__main__':main()
