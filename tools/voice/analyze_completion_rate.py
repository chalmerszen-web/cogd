"""Offline reference match at normal and erroneous 1.5x durations; no playback."""
import argparse
import json
from pathlib import Path

import numpy as np
from scipy import signal

from analyze_latency import correlate, load, sha


def match(reference, recording):
    rows = []
    for low, high in ((1000,2000),(2000,3500)):
        sos = signal.butter(4,[low,high],fs=16000,btype='bandpass',output='sos')
        if len(recording) < len(reference):
            return dict(score=None, reason='Template does not fit recording')
        curve = np.abs(correlate(signal.sosfiltfilt(sos,reference),
                                signal.sosfiltfilt(sos,recording)))
        at = int(np.argmax(curve))
        rows.append(dict(band_hz=[low,high], start_s=at/16000, score=float(curve[at])))
    return dict(score=float(np.mean([r['score'] for r in rows])), bands=rows,
                spread_s=max(r['start_s'] for r in rows)-min(r['start_s'] for r in rows))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=Path)
    p.add_argument('--assets',type=Path,required=True)
    args = p.parse_args()
    target = args.directory/'completion-rate-analysis.json'
    if target.exists():
        raise FileExistsError(target)
    report = json.loads((args.directory/'offline-analysis.json').read_text(encoding='utf8'))
    raw = (args.assets/'completion_zh/reference.pcm').read_bytes()
    reference = np.frombuffer(raw,dtype='<i2').astype(float)/32768
    wrong = signal.resample_poly(reference,3,2)
    result = dict(accepted=False, script_sha256=sha(Path(__file__)), reference_sha256=sha(args.assets/'completion_zh/reference.pcm'),
        criteria='Normal mean >=.55, each band >=.4, peaks within20ms, margin >=.15 over1.5x',
        scope='Waveform rate consistency only; not subjective quality or full voice acceptance', rows=[])
    for trial in report['trials']:
        speech = trial.get('offline_asr')
        row = dict(round=trial['round'], rate_consistent=False)
        if speech:
            path = Path(speech['file'])
            assert sha(path) == speech['sha256']
            recording = load(path)
            normal, slow = match(reference,recording), match(wrong,recording)
            good = normal['score'] is not None and slow['score'] is not None and normal['score'] >= .55 and (
                min(r['score'] for r in normal['bands']) >= .4 and normal['spread_s'] <= .02 and
                normal['score']-slow['score'] >= .15)
            row.update(rate_consistent=good, normal=normal, wrong_rate=slow,
                       crop_sha256=speech['sha256'], local_asr=speech['text'])
        result['rows'].append(row)
    if not report['trials']:
        # Failed user-input alignment forbids latency claims. The known output
        # can still be matched independently within the full saved recording.
        path=args.directory/'speaker.wav'
        assert sha(path)==report['recording_sha256']
        recording=load(path)
        curves=[]
        for low,high in ((1000,2000),(2000,3500)):
            sos=signal.butter(4,[low,high],fs=16000,btype='bandpass',output='sos')
            curves.append(np.abs(correlate(signal.sosfiltfilt(sos,reference),signal.sosfiltfilt(sos,recording))))
        scores=np.mean(curves,axis=0)
        peaks,_=signal.find_peaks(scores,height=.55,distance=2*16000)
        result['independent_asset_matches']=[]
        for at in peaks:
            begin=max(0,int(at)-3200)
            end=min(len(recording),int(at)+len(wrong)+3200)
            normal,slow=match(reference,recording[begin:end]),match(wrong,recording[begin:end])
            good=(slow['score'] is not None and min(r['score'] for r in normal['bands'])>=.4 and
                  normal['spread_s']<=.02 and normal['score']-slow['score']>=.15)
            result['independent_asset_matches'].append(dict(recording_start_s=int(at)/16000,
                rate_consistent=good,normal=normal,wrong_rate=slow))
        result['latency_status']='Unavailable; original input-alignment failure retained'
    target.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(result,ensure_ascii=False))


if __name__ == '__main__':
    main()
