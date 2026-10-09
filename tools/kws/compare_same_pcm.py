"""Frozen E/F versus E/K on identical validation microphone PCM, no fitting."""
import hashlib
import json
from pathlib import Path
import sys
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from data import load_pcm
from append_captures import capture_path
from device_domain import pcm,align,align_waveform
from evaluate import events
from diagnose_fusion_captures import score


def main():
    base=ROOT/'artifacts/kws-phase6'
    out=base/'same-pcm-comparison.json'
    assert not out.exists()
    corpus=base/'comparison-captures/report.json'
    report=json.loads(corpus.read_text(encoding='utf8'))
    assert report['complete'] and report['comparison_only'] and len(report['trials'])==12
    libraries={'ef':ROOT/'build-kws-fusion-host/libkws.so','ek':ROOT/'build-kws-fusion-ek-host/libkws.so'}
    frozen={}
    for pair,path in libraries.items():
        directory=ROOT/'artifacts/kws-phase5'/('fusion-'+pair+'-smooth3')
        parity=json.loads((directory/'fusion-host-parity.json').read_text())
        assert hashlib.sha256(path.read_bytes()).hexdigest()==parity['library_sha256']
        frozen[pair]=dict(library_sha256=parity['library_sha256'],
            threshold=json.loads((directory/'threshold.json').read_text())['quantized'])
    results=[]
    for trial in report['trials']:
        row=trial['source'];assert row['split']=='validation'
        signal=pcm(capture_path(trial['path']))
        assert hashlib.sha256(signal.tobytes()).hexdigest()==trial['signal']['sha256']
        expected=trial['ready']['capture_samples']+16000*(trial['audio']['source_started']-trial['ready_query_finished'])
        result=dict(clip_id=row['clip_id'],language=row['language'],label=row['label'],text=row['text'],
                    gain=row['playback_gain'],adc_clipped=trial['state']['record_adc_clipped'],models={})
        low=high=None
        if row['label']:
            original=load_pcm(row)
            try:
                try:lag,quality=align(original,signal,expected)
                except ValueError:lag,quality=align_waveform(original,signal,expected)
                low=lag+row['speech_end_low_sample']-320
                high=lag+row['speech_end_high_sample']+320
                result['alignment']=dict(lag=lag,quality=quality,accept=[low,high+12800])
            except ValueError as error:result['alignment_error']=str(error)
        padded=np.concatenate([np.resize(signal[:4096],32768),signal])
        for pair,library in libraries.items():
            values=score(padded,library)
            detected=[e-32768 for e in events(values,frozen[pair]['threshold']) if e>=32768]
            result['models'][pair]=dict(events=detected,triggered=bool(detected),
                valid_hit=bool(row['label'] and low is not None and any(low<=e<=high+12800 for e in detected)))
        results.append(result)
    summary={}
    for pair in libraries:
        summary[pair]=dict(positive_hits=sum(r['models'][pair]['valid_hit'] for r in results),
            positive_total=sum(r['label'] for r in results),
            negative_triggers=sum(not r['label'] and r['models'][pair]['triggered'] for r in results),
            negative_total=sum(not r['label'] for r in results))
    with out.open('x',encoding='utf8') as f:json.dump(dict(complete=True,frozen=frozen,summary=summary,trials=results,
        report_sha256=hashlib.sha256(corpus.read_bytes()).hexdigest(),training=False,threshold_changed=False,
        limitation='Same raw PCM isolates model/threshold difference; repeated ambient prefill is not exact live state. Synthetic validation sources, not blind voices/metres.'),f,ensure_ascii=False,indent=2)
    print(json.dumps(summary))


if __name__=='__main__':main()
