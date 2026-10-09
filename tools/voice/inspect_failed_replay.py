"""Broad offline stimulus inspection after alignment failure; never latency acceptance."""
import argparse
import json
from pathlib import Path

import numpy as np
from scipy import signal
import sherpa_onnx
import soundfile as sf

from analyze_latency import correlate, load, sha


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=Path)
    args=p.parse_args()
    out=args.directory/'replay-inspection'
    out.mkdir(exist_ok=False)
    report=json.loads((args.directory/'report.json').read_text(encoding='utf8'))
    alignment=json.loads((args.directory/'offline-analysis.json').read_text(encoding='utf8'))
    assert not alignment['source_alignment']['accepted']
    recording=load(args.directory/'speaker.wav')
    source=load(args.directory/'prompt.wav')
    base=alignment['source_alignment']['fitted_first_start_recording_s']
    starts=alignment['source_alignment']['relative_playback_times_s']
    recognizer=sherpa_onnx.OfflineRecognizer.from_sense_voice(
        model='artifacts/kws-phase2/asr/model.int8.onnx',
        tokens='artifacts/kws-phase2/asr/tokens.txt',num_threads=4,use_itn=True,language='auto')
    result=dict(accepted=False,latency_acceptance=False,script_sha256=sha(Path(__file__)),
        recording_sha256=sha(args.directory/'speaker.wav'),
        scope='Wide independent searches may diagnose stimulus; original failed common-clock gate remains failed',rows=[])
    for trial,dt in zip(report['trials'],starts):
        a=max(0,base+dt-1.5);b=min(len(recording)/16000,base+dt+len(source)/16000+1.5)
        crop=recording[round(a*16000):round(b*16000)]
        path=out/f'input-{trial["round"]}.wav'
        sf.write(path,crop,16000,subtype='PCM_16')
        stream=recognizer.create_stream();stream.accept_waveform(16000,crop.astype(np.float32))
        recognizer.decode_stream(stream)
        bands=[]
        for low,high in ((1000,2000),(2000,3500)):
            sos=signal.butter(4,[low,high],fs=16000,btype='bandpass',output='sos')
            curve=correlate(signal.sosfiltfilt(sos,source),signal.sosfiltfilt(sos,crop))
            at=int(np.argmax(curve))
            bands.append(dict(band_hz=[low,high],start_s=a+at/16000,correlation=float(curve[at])))
        row=dict(round=trial['round'],crop_s=[a,b],crop_sha256=sha(path),
                 offline_asr=stream.result.text,device_asr=trial['asr'],independent_template_peaks=bands)
        result['rows'].append(row)
    (out/'report.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(result,ensure_ascii=False))


if __name__=='__main__':
    main()
