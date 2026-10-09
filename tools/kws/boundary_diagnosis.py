"""Bounded analysis of 20 frozen examples; never train, rescore or flash."""
import csv
import hashlib
import html
import json
from pathlib import Path
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf
import sherpa_onnx

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from data import load_pcm,framed_example
from evaluate import events


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def render_plot(row,r,pcm,raw_scores,threshold,start,out):
    ordinal=r['number'];clip=r['clip_id']
    local_trigger=None if r['trigger_local_ms'] is None else r['trigger_local_ms']*16
    padded=np.pad(pcm.astype(float),(0,(-len(pcm))%160))
    rms=np.sqrt(np.mean(padded.reshape(-1,160)**2,axis=1))
    fig,axes=plt.subplots(3,1,figsize=(10,7),sharex=True)
    tx=np.arange(len(rms))*.01+.005
    axes[0].plot(tx,20*np.log10(np.maximum(rms,1)/32768),lw=1)
    axes[0].set_ylabel('RMS dBFS');axes[0].set_ylim(-90,0)
    axes[1].specgram(pcm.astype(float)/32768,NFFT=256,Fs=16000,noverlap=192,cmap='magma',vmin=-110,vmax=-30)
    axes[1].set_ylim(0,5000);axes[1].set_ylabel('Frequency Hz')
    score_t=((np.arange(len(raw_scores))+1)*256-start)/16000
    axes[2].plot(score_t,raw_scores/256,lw=1,label='Frozen INT8 logit')
    axes[2].axhline(threshold/256,color='gray',ls='--',label='Frozen threshold')
    axes[2].axvline((32768-start)/16000,color='purple',ls=':',label='First eligible block')
    axes[2].set_ylabel('Logit');axes[2].set_xlabel('Seconds in retained source clip')
    for ax in axes:
        if row['label']:ax.axvline(row['wake_end_sample']/16000,color='black',ls='--')
        if local_trigger is not None:ax.axvline(local_trigger/16000,color='red')
        ax.grid(alpha=.15);ax.set_xlim(0,min(max(len(pcm)/16000+.16, (local_trigger or 0)/16000+.08),4.096))
    axes[2].legend(fontsize=7)
    fig.suptitle(f'{ordinal:02d} {clip}\nred=trigger, black=annotation end; {r["role"]}',fontsize=10)
    fig.tight_layout();fig.savefig(out/r['plot'],dpi=130);plt.close(fig)


def run(output_dir=None):
    base=ROOT/'artifacts/kws-phase2'
    out=Path(output_dir).resolve() if output_dir else ROOT/'artifacts/kws-boundary-diagnosis'
    out.mkdir(parents=True,exist_ok=False)
    frozen=json.loads((base/'final-stage-audit.json').read_text())['files']
    def check_frozen():
        for row in frozen:
            assert sha(ROOT/row['path'])==row['sha256'],row['path']
    check_frozen()
    diagnosis=json.loads((base/'final-diagnosis.json').read_text())
    rows={r['clip_id']:r for r in map(json.loads,(base/'dataset-final/manifest.jsonl').read_text(encoding='utf8').splitlines())}
    originals={r['clip_id']:r for language in ('zh','yue') for r in map(json.loads,
        (base/f'data/dataset-{language}-final1.jsonl').read_text(encoding='utf8').splitlines())}
    data=np.load(base/'features-final/test.npz',allow_pickle=False)
    scores=np.load(base/'adjustment2-int8/test-scores.npz',allow_pickle=False)
    np.testing.assert_array_equal(data['clip_id'],scores['clip_id'])
    indices={str(clip):i for i,clip in enumerate(data['clip_id'])}
    threshold=json.loads((base/'adjustment2-int8/threshold.json').read_text())['quantized']
    selection=[]
    for group in ('kokoro-v1-af_heart','kokoro-v1-am_michael'):
        failures=sorted([r for r in diagnosis['positive_failures'] if r['language']=='yue' and
            r['kind']=='early' and r['source_group']==group],key=lambda r:(r['event_offsets_ms'][0],r['clip_id']))
        for i in np.linspace(0,len(failures)-1,6).round().astype(int):
            selection.append(dict(clip_id=failures[i]['clip_id'],role='yue_early_stratified'))
    for r in diagnosis['positive_failures']:
        if r['language']=='zh' or r['kind']=='no_event':
            selection.append(dict(clip_id=r['clip_id'],role=r['language']+'_'+r['kind']))
    for group in ('kokoro-v1-af_heart','kokoro-v1-am_michael'):
        for clip,row in sorted(rows.items()):
            if row['split']!='test' or row['language']!='yue' or not row['label'] or row['source_group']!=group:continue
            i=indices[clip];end=int(data['event_end'][i]);times=events(scores['quantized'][i],threshold)
            if any(end<=t<=end+12800 for t in times):
                selection.append(dict(clip_id=clip,role='yue_hit_control'));break
    for r in diagnosis['negative_triggers']:
        selection.append(dict(clip_id=r['clip_id'],role='negative_trigger'))
    assert len(selection)==len({r['clip_id'] for r in selection})==20
    for item in selection:
        row=rows[item['clip_id']]
        item.update(path=row['path'],pcm_sha256=row['pcm_sha256'],language=row['language'],source_group=row['source_group'])
    (out/'selection.json').write_text(json.dumps(selection,indent=2,ensure_ascii=False),encoding='utf8')
    # Selection is committed to disk before analysing any waveforms.
    model=base/'asr/model.int8.onnx';tokens=base/'asr/tokens.txt'
    recognizers={lang:sherpa_onnx.OfflineRecognizer.from_sense_voice(model=str(model),tokens=str(tokens),
        num_threads=4,use_itn=True,language=lang) for lang in ('zh','yue')}
    def decode(pcm,lang):
        samples=np.pad(np.asarray(pcm,dtype=np.float32)/32768,(1600,4800))
        rec=recognizers[lang];stream=rec.create_stream();stream.accept_waveform(16000,samples);rec.decode_stream(stream)
        result=stream.result
        return dict(text=result.text,language=result.lang,tokens=list(result.tokens),
            timestamps_seconds=[round(float(t)-.1,4) for t in result.timestamps],
            note='Approximate CTC emissions, not syllable end truth; same ASR family as curation.')
    reports=[]
    for ordinal,item in enumerate(selection,1):
        clip=item['clip_id'];row=rows[clip];i=indices[clip]
        pcm=load_pcm(row);window,_,end=framed_example(row,pcm)
        assert (end if end is not None else -1)==int(data['event_end'][i])
        times=events(scores['quantized'][i],threshold)
        start=end-(row['wake_end_sample']-row['wake_start_sample']) if row['label'] else 0
        trigger=times[0] if times else None
        local_trigger=trigger-start if trigger is not None else None
        full_asr=decode(pcm,row['language'])
        prefix_asr=decode(pcm[:max(0,min(len(pcm),local_trigger))],row['language']) if local_trigger else None
        original=originals[clip];original_pcm=load_pcm(original)
        original_asr=decode(original_pcm,row['language']) if row.get('carrier') and row['label'] else None
        # Energy evidence uses an independent 10 ms frame, no audio normalization.
        padded=np.pad(pcm.astype(float),(0,(-len(pcm))%160))
        rms=np.sqrt(np.mean(padded.reshape(-1,160)**2,axis=1))
        peak=max(float(rms.max()),1.)
        ends={}
        for db in (20,30,40):
            active=np.flatnonzero(rms>=max(1.,peak*10**(-db/20)))
            ends[str(db)]=min(len(pcm),int(active[-1]+1)*160)/16 if len(active) else None
        clipped_trigger=max(0,min(len(pcm),local_trigger)) if local_trigger is not None else None
        remaining=pcm[clipped_trigger:].astype(float) if clipped_trigger is not None else None
        energy=float(np.sum(pcm.astype(float)**2))
        waveform=out/f'{ordinal:02d}-full.wav';sf.write(waveform,pcm,16000,subtype='PCM_16')
        prefix_path=None
        if clipped_trigger is not None and clipped_trigger>0:
            prefix_path=out/f'{ordinal:02d}-prefix.wav';sf.write(prefix_path,pcm[:clipped_trigger],16000,subtype='PCM_16')
        raw_scores=scores['quantized'][i];block_scores=raw_scores[1::2]
        before_warmup=int(np.count_nonzero(block_scores[:63]>=threshold))
        r=dict(number=ordinal,**item,label=row['label'],source_text=row.get('source_text'),
            original_path=original['path'],original_pcm_sha256=original['pcm_sha256'],
            crop_start=row.get('crop_start'),crop_end=row.get('crop_end'),
            duration_ms=len(pcm)/16,annotation_end_ms=row['wake_end_sample']/16 if row['label'] else None,
            trigger_local_ms=local_trigger/16 if local_trigger is not None else None,
            event_offsets_ms=[(t-end)/16 for t in times] if end else None,
            energy_end_ms_below_peak=ends,remaining_energy_fraction=float(np.sum(remaining**2))/energy if remaining is not None and energy else None,
            score_blocks_above_threshold_before_warmup=before_warmup,
            trigger_at_first_eligible_block=trigger==32768,full_asr=full_asr,prefix_asr=prefix_asr,
            original_asr=original_asr,plot=f'{ordinal:02d}.png',audio=waveform.name,
            prefix_audio=prefix_path.name if prefix_path else None)
        reports.append(r)
        render_plot(row,r,pcm,raw_scores,threshold,start,out)
        print(json.dumps({k:r[k] for k in ('number','clip_id','trigger_local_ms','energy_end_ms_below_peak','remaining_energy_fraction')},ensure_ascii=False),flush=True)
    check_frozen()
    report=dict(scope='20-source post-test diagnosis only; no new KWS inference or training; no human listening claim',
        original_evidence_unchanged=True,script_sha256=sha(Path(__file__)),asr_model_sha256=sha(model),
        selection_sha256=sha(out/'selection.json'),records=reports)
    (out/'analysis.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf8')
    with (out/'summary.csv').open('w',encoding='utf-8-sig',newline='') as f:
        fields=['number','clip_id','role','trigger_local_ms','duration_ms','remaining_energy_fraction','full_text','prefix_text']
        writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader()
        for r in reports:
            writer.writerow({**{k:r[k] for k in fields[:-2]},'full_text':r['full_asr']['text'],
                'prefix_text':r['prefix_asr']['text'] if r['prefix_asr'] else ''})
    pages=['<!doctype html><meta charset="utf-8"><title>小言：20条边界诊断</title>',
        '<style>body{font:16px sans-serif;max-width:1000px;margin:24px auto}img{width:100%}section{border-top:1px solid #ccc;padding:20px 0}audio{width:44%}</style>',
        '<h1>20条有限诊断</h1><p>原成绩未更改。自动转写不是人工听审，CTC时间戳不是音素结束真值。红线为触发，黑虚线为原标注结束。</p>']
    for r in reports:
        pages.extend([f'<section><h2>{r["number"]:02d} {html.escape(r["clip_id"])}</h2>',
            f'<p>{html.escape(r["role"])}；原文本：{html.escape(r["source_text"] or "")}</p>',
            f'<p>完整自动转写：{html.escape(r["full_asr"]["text"])}</p>',
            f'<p>截至触发自动转写：{html.escape(r["prefix_asr"]["text"] if r["prefix_asr"] else "无触发")}</p>',
            f'<audio controls src="{r["audio"]}"></audio>',
            f'<audio controls src="{r["prefix_audio"]}"></audio>' if r['prefix_audio'] else '',
            f'<img src="{r["plot"]}"></section>'])
    (out/'index.html').write_text('\n'.join(pages),encoding='utf8')
    print('BOUNDARY_DIAGNOSIS_COMPLETE',flush=True)


def plots_only():
    base=ROOT/'artifacts/kws-phase2';out=ROOT/'artifacts/kws-boundary-diagnosis'
    analysis=json.loads((out/'analysis.json').read_text(encoding='utf8'))
    rows={r['clip_id']:r for r in map(json.loads,(base/'dataset-final/manifest.jsonl').read_text(encoding='utf8').splitlines())}
    scores=np.load(base/'adjustment2-int8/test-scores.npz',allow_pickle=False)
    ids={str(c):i for i,c in enumerate(scores['clip_id'])}
    threshold=json.loads((base/'adjustment2-int8/threshold.json').read_text())['quantized']
    for r in analysis['records']:
        row=rows[r['clip_id']];pcm=load_pcm(row);_,_,end=framed_example(row,pcm)
        start=end-len(pcm) if row['label'] else 0
        render_plot(row,r,pcm,scores['quantized'][ids[r['clip_id']]],threshold,start,out)
    print('20 plots refreshed; no audio decoding or inference rerun')


def summarize():
    base=ROOT/'artifacts/kws-phase2';out=ROOT/'artifacts/kws-boundary-diagnosis'
    report=json.loads((out/'analysis.json').read_text(encoding='utf8'))
    scores=np.load(base/'adjustment2-int8/test-scores.npz',allow_pickle=False)
    ids={str(c):i for i,c in enumerate(scores['clip_id'])}
    threshold=json.loads((base/'adjustment2-int8/threshold.json').read_text())['quantized']
    records=[]
    for r in report['records']:
        values=scores['quantized'][ids[r['clip_id']]][1::2]
        peaks=[dict(block=int(k+1),sample_end=int((k+1)*512),score_q8=int(values[k]))
               for k in np.argsort(values)[-5:][::-1]]
        above=[dict(block=k+1,score_q8=int(values[k]),votes_above_in_last3=int(np.sum(values[max(0,k-2):k+1]>=threshold)))
               for k in range(63,len(values)) if values[k]>=threshold]
        records.append(dict(number=r['number'],clip_id=r['clip_id'],threshold_q8=threshold,
            eligible_above_threshold=above,top5blocks=peaks))
    early=[r for r in report['records'] if r['role']=='yue_early_stratified']
    gaps=[r['annotation_end_ms']-r['energy_end_ms_below_peak']['40'] for r in early]
    delays=[r['trigger_local_ms']-r['energy_end_ms_below_peak']['40'] for r in early]
    summary=dict(yue_early_count=len(early),
        annotation_tail_ms_at_minus40=dict(min=min(gaps),median=float(np.median(gaps)),max=max(gaps)),
        trigger_after_energy_end_ms_at_minus40=dict(min=min(delays),median=float(np.median(delays)),max=max(delays)),
        max_remaining_energy_percent=max(r['remaining_energy_fraction'] for r in early)*100)
    result=dict(summary=summary,records=records);path=out/'score-diagnostics.json'
    if path.exists():assert json.loads(path.read_text(encoding='utf8'))==result
    else:path.write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps(summary,indent=2))


if __name__=='__main__':
    if sys.argv[1:]==['--plots-only']:plots_only()
    elif sys.argv[1:]==['--summarize']:summarize()
    elif len(sys.argv)==3 and sys.argv[1]=='--out':run(sys.argv[2])
    elif sys.argv[1:]:raise SystemExit('Use --out NEW_DIRECTORY, --plots-only or --summarize')
    else:run()
