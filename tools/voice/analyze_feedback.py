"""Offline cue spectrum and actual stage-notice transcription; never an answer metric."""
import argparse
import json
from pathlib import Path
import numpy as np
from scipy import signal
import soundfile as sf
from analyze_continuous import ROOT, RATE, event, load, sha


def analyze(folder, recognizer):
    target = folder/'feedback-analysis.json'
    if target.exists():
        raise FileExistsError(target)
    report = json.loads((folder/'report.json').read_text(encoding='utf8'))
    prior = json.loads((folder/'offline-analysis.json').read_text(encoding='utf8'))
    assert prior['report_sha256'] == sha(folder/'report.json')
    recording = load(folder/'speaker.wav')
    (folder/'offline-clips').mkdir(exist_ok=True)
    result = dict(report_sha256=sha(folder/'report.json'), script_sha256=sha(Path(__file__)),
                  limitations=['Fixed playback, not human acceptance.',
                               'Swoosh and cached process speech are never useful-answer responses.',
                               'Spectral match is a bounded presence check, not exact phoneme timing.'], trials=[])
    attempts = [(t,a) for t in report['trials'] for a in t['attempts'] if a.get('prompt_playback')]
    matches = prior.get('source_alignment', {}).get('copies', [])
    for (trial, attempt), match in zip(attempts,matches):
        events = attempt['events']
        anchor = event(events,'playback')
        row = dict(round=trial['round'], accepted_input_alignment=match.get('accepted',False))
        result['trials'].append(row)
        if not anchor or not match.get('accepted'):
            row['unavailable']='Missing final playback or accepted source alignment'
            continue
        origin = match['source_start_recording_s'] + anchor['observed'] - attempt['prompt_playback']['source_started']
        def location(e):
            return origin + (e['time_ms']-anchor['time_ms'])/1000
        begin,end=event(events,'endpoint_cue_start'),event(events,'endpoint_cue_end')
        if begin and end:
            nominal=location(begin)
            lo,hi=max(0,nominal-.35),min(len(recording)/RATE,nominal+.55)
            f,t,p=signal.spectrogram(recording[round(lo*RATE):round(hi*RATE)],fs=RATE,
                                     nperseg=512,noverlap=432,mode='magnitude')
            band=(f>=700)&(f<=3000)
            peaks=f[band][np.argmax(p[band],axis=0)]
            candidates=[]
            for start in np.arange(nominal-.30,nominal+.301,.005):
                age=t+lo-start; valid=(age>=.02)&(age<=.14)
                expected=2600-1700*age/.18
                score=float(np.mean(abs(peaks[valid]-expected[valid])<150)) if np.count_nonzero(valid)>=20 else 0
                candidates.append((score,float(start)))
            score,start=max(candidates)
            row['cue']=dict(board_begin_ms=begin['time_ms'],board_end_ms=end['time_ms'],
                            spectrum_match_fraction=score,candidate_start_recording_s=start,
                            descending_sweep_present=score>=.75,threshold=.75)
        start,finish=event(events,'stage_speaker_start'),event(events,'stage_end')
        notice=event(events,'stage_start')
        if start and finish and notice:
            phrases={
                '我在查之前的记录。':(1.2746875,'context_search'),
                '我而家查紧之前嘅记录。':(1.443125,'context_search'),
                '我来保存。':(13613/16000,'context_summary_set'),
                '我记低先。':(13079/16000,'context_summary_set')}
            if notice.get('text') not in phrases:
                row['stage_unavailable']='Unknown cached phrase; duration not inferred'
                continue
            duration,operation=phrases[notice['text']]
            lo=max(0,location(start)-.20)
            hi=min(len(recording)/RATE,location(start)+duration+.30,location(finish)+.10)
            assert hi>lo
            pcm=recording[round(lo*RATE):round(hi*RATE)].astype(np.float32)
            file=folder/'offline-clips'/f'round-{trial["round"]:02}-stage.wav'
            sf.write(file,pcm,RATE,subtype='PCM_16')
            stream=recognizer.create_stream();stream.accept_waveform(RATE,pcm)
            recognizer.decode_stream(stream)
            row['stage_notice']=dict(expected=notice.get('text'),offline_asr=stream.result.text.strip(),
                                      file=str(file),sha256=sha(file),crop_s=[lo,hi],
                                      source='cached phrase triggered by context tool batch',operation=operation)
    target.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(dict(directory=str(folder),trials=result['trials']),ensure_ascii=False))


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directories',type=Path,nargs='+')
    args=p.parse_args()
    import sherpa_onnx
    model=ROOT/'artifacts/kws-phase2/asr'
    recognizer=sherpa_onnx.OfflineRecognizer.from_sense_voice(model=str(model/'model.int8.onnx'),
                    tokens=str(model/'tokens.txt'),num_threads=4,use_itn=True,language='auto')
    for folder in args.directories:
        analyze(folder,recognizer)
