"""Offline frozen-C comparison using exported post-decimation board PCM."""
import json
from pathlib import Path
import sys
import wave
import hashlib
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/kws'),str(ROOT/'training/kws')]
from parity import Backend
from evaluate import events


def main():
    out=ROOT/'artifacts/kws-phase4'
    backend=Backend(ROOT/'build-kws-phase3-host/libkws.so','kws_trained_model')
    inputs=[]
    calibration=json.loads((out/'calibration-final/report.json').read_text(encoding='utf8'))
    for row in calibration['selection']:
        lang=row['language']
        inputs.extend([(f'source-{lang}',ROOT/row['path']),
                       (f'played-source-{lang}',out/'calibration-final/leveled-sources'/(row['clip_id']+'.wav')),
                       (f'board-capture-{lang}',out/f'calibration-final/device-{lang}.wav')])
    selected=json.loads((out/'formal/selection.json').read_text(encoding='utf8'))
    inputs.extend((r['clip_id'],ROOT/r['path']) for r in selected if r['label'])
    records=[]
    for name,path in inputs:
        with wave.open(str(path),'rb') as f:
            assert (f.getframerate(),f.getsampwidth(),f.getnchannels())==(16000,2,1)
            raw=f.readframes(f.getnframes())
        pcm=np.frombuffer(raw,dtype='<i2')
        padded=np.pad(pcm,(35840,16000))
        padded=np.pad(padded,(0,(-len(padded))%512)).astype(np.int16)
        backend.lib.kws_reset(backend.handle)
        scores=np.array([backend.pcm(frame).layer[-1] for frame in padded.reshape(-1,256)])
        record=dict(name=name,path=path.relative_to(ROOT).as_posix(),pcm_sha256=hashlib.sha256(raw).hexdigest(),
                    max_score_q8=int(scores.max()),events_samples=events(scores,625),
                    scores_q8=scores.tolist(),rms=float(np.sqrt(np.mean(pcm.astype(float)**2))))
        records.append(record)
    report=dict(threshold_q8=625,prefix_samples=35840,records=records,
                limitation='Manual clip and wake both receive the same post-decimation/DC removal PCM in mic_update; manual capture is a separate run, not simultaneous formal-test input. Synthetic zero prefix differs from live ambient.')
    (out/'offline-acoustic-diagnosis.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print(json.dumps([dict(name=r['name'],max_score=r['max_score_q8'],events=len(r['events_samples'])) for r in records[:6]]))


if __name__=='__main__':main()
