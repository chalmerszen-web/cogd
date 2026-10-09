"""Validate new TRAIN captures before feature generation; no model scoring."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from device_domain import align,align_waveform,pcm
from data import read_manifest,load_pcm
from append_captures import capture_path


def main():
    p=argparse.ArgumentParser();p.add_argument('--corpus',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--expected-trials',type=int,choices=(24,32,48),default=48);args=p.parse_args()
    report=json.loads((args.corpus/'report.json').read_text(encoding='utf8'))
    assert report['complete'] and len(report['trials'])==args.expected_trials
    parents={r['clip_id']:r for r in read_manifest(ROOT/'artifacts/kws-phase3/dataset/manifest.jsonl')}
    result=[]
    for trial in report['trials']:
        row=trial['source'];parent=parents[row['parent_clip_id']]
        assert row['split']==parent['split']=='train'
        assert row['source_group']==parent['source_group']
        source=load_pcm(parent);recorded=pcm(capture_path(trial['path']))
        assert hashlib.sha256((ROOT/parent['path']).read_bytes()).hexdigest()==row['source_wav_sha256']
        assert len(recorded)==96000
        assert hashlib.sha256(recorded.tobytes()).hexdigest()==trial['signal']['sha256']
        expected=trial['ready']['capture_samples']+16000*(trial['audio']['source_started']-trial['ready_query_finished'])
        entry=dict(clip_id=row['clip_id'],parent_clip_id=parent['clip_id'],language=row['language'],
            label=row['label'],text=row['text'],gain=row['playback_gain'],
            source_group=row['source_group'],recording=trial['path'],
            capture_adc_clipped=trial['state']['record_adc_clipped'],pcm_fullscale_samples=int((np.abs(recorded.astype(int))>=32767).sum()))
        try:
            try:
                lag,correlation=align(source,recorded,expected)
                alignment=dict(method='bounded_rms_envelope',correlation=correlation)
            except ValueError:
                lag,alignment=align_waveform(source,recorded,expected);correlation=None
            assert trial['ready']['capture_stage']==2 and trial['state']['capture_error']=='ok'
            noise=recorded[-16000:].astype(float);speech=recorded[lag:lag+len(source)].astype(float)
            entry.update(aligned=True,lag_samples=lag,correlation=correlation,alignment=alignment,
                noise_rms=float(np.sqrt(np.mean(noise**2))),received_rms=float(np.sqrt(np.mean(speech**2))))
        except (ValueError,AssertionError) as error:entry.update(aligned=False,error=str(error))
        entry['eligible']=entry['aligned'] and entry['pcm_fullscale_samples']==0 and entry['capture_adc_clipped']==0
        result.append(entry)
    summary={language:{str(gain):dict(total=sum(r['language']==language and r['gain']==gain for r in result),
        eligible=sum(r['language']==language and r['gain']==gain and r['eligible'] for r in result))
        for gain in sorted({r['gain'] for r in result})} for language in ('zh','yue')}
    audit=dict(complete=True,train_only=True,summary=summary,trials=result,
        source_report_sha256=hashlib.sha256((args.corpus/'report.json').read_bytes()).hexdigest(),
        limitation='Controlled synthetic-source replay; uncontrolled room ambient; not phonetic or five-metre validation.')
    with args.out.open('x',encoding='utf8') as f:json.dump(audit,f,ensure_ascii=False,indent=2)
    print(json.dumps(summary),flush=True)


if __name__=='__main__':main()
