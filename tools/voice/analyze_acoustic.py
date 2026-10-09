"""Offline recognition of only the device reply in external microphone recordings."""
import argparse
import json
import math
from pathlib import Path
import numpy as np
import scipy.signal
import soundfile as sf
import sherpa_onnx
from analyze_latency import align, load as aligned_wave, envelope

ROOT=Path(__file__).resolve().parents[2]


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path)
    a=p.parse_args();report=json.loads((a.directory/'report.json').read_text(encoding='utf8'))
    model=ROOT/'artifacts/kws-phase2/asr'
    recognizer=sherpa_onnx.OfflineRecognizer.from_sense_voice(
        model=str(model/'model.int8.onnx'),tokens=str(model/'tokens.txt'),
        num_threads=4,use_itn=True,language='auto')
    rows=[]
    for i,trial in enumerate(report.get('trials',[report])):
        folder=a.directory/f'trial-{i+1:02}' if 'trials' in report else a.directory
        audio,rate=sf.read(folder/'speaker.wav',dtype='float32')
        if audio.ndim==2:audio=audio.mean(axis=1)
        playback=next((e for e in trial.get('events',[]) if e['stage']=='playback'),None)
        if not playback and not trial.get('serial_closed_during_turn'):
            rows.append({'trial':i+1,'error':'No device playback event'});continue
        # Recorder readiness is not WAV zero. Prefer the source-correlated
        # clock, and exclude source speech when a fast answer follows closely.
        start=max(0,playback['observed']-trial['record_started']-.75) if playback else max(
            0,trial['utterance_playback']['finished']-trial['record_started']+1)
        crop_method='uncalibrated legacy event margin'
        source=folder/'utterance.wav'
        if source.exists() and playback and trial.get('utterance_playback',{}).get('source_started') is not None:
            original=aligned_wave(source);match=align(original,aligned_wave(folder/'speaker.wav'))
            if not match['accepted']:
                rows.append({'trial':i+1,'error':'Ambiguous acoustic source alignment'});continue
            energies=envelope(original)
            active=np.flatnonzero(energies>=max(64/32768,energies.max()*.02))
            source_end=match['offset_s']+(int(active[-1])+1)*.01
            anchor=match['offset_s']+playback['observed']-trial['utterance_playback']['source_started']
            start=max(source_end+.04,anchor-.75)
            crop_method='source-correlated WAV clock; exclude input tail; event only bounds search'
        reply=audio[round(start*rate):]
        sf.write(folder/'reply-only.wav',reply,rate,subtype='PCM_16')
        stream=recognizer.create_stream();g=math.gcd(rate,16000)
        stream.accept_waveform(16000,scipy.signal.resample_poly(reply,16000//g,rate//g))
        recognizer.decode_stream(stream)
        row={'trial':i+1,'start_seconds':start,'crop_method':crop_method,'duration_seconds':len(reply)/rate,
             'transcript':stream.result.text,'peak':float(np.max(np.abs(reply))),
             'rms':float(np.sqrt(np.mean(reply**2))),
             'full_scale_samples':int(np.count_nonzero(np.abs(reply)>=32767/32768))}
        rows.append(row);print(json.dumps(row,ensure_ascii=False),flush=True)
    (a.directory/'external-asr.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf8')


if __name__=='__main__':main()
