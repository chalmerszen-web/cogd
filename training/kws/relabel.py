"""Create a separate endpoint-labelled corpus; preserve all historical inputs."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
import soundfile as sf
import sherpa_onnx

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from boundary import CONFIG,interval,place


def main():
    ap=argparse.ArgumentParser();ap.add_argument('manifests',type=Path,nargs='+')
    ap.add_argument('--out',type=Path,required=True);ap.add_argument('--retire-test',action='store_true')
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    base=ROOT/'artifacts/kws-phase2/asr';recognizers={}
    accepted=[];rejected=[];retired=[]
    for path in args.manifests:
        for line in path.read_text(encoding='utf8').splitlines():
            row=json.loads(line);original=row.copy()
            if args.retire_test and row['split']=='test':
                row['split']='train' if row['source_group'].startswith('kokoro-') else 'validation'
                retired.append(dict(clip_id=row['clip_id'],old_split='test',new_split=row['split'],pcm_sha256=row['pcm_sha256']))
            # Natural/procedural negatives have no target endpoint to annotate.
            if not row['source_group'].startswith('kokoro-'):
                accepted.append(row);continue
            pcm,rate=sf.read(ROOT/row['path'],dtype='int16');assert rate==16000
            assert hashlib.sha256(pcm.astype('<i2').tobytes()).hexdigest()==row['pcm_sha256']
            crop_end=len(pcm)
            try:
                if not row['label'] and row.get('carrier'):
                    lang=row['language']
                    if lang not in recognizers:
                        recognizers[lang]=sherpa_onnx.OfflineRecognizer.from_sense_voice(model=str(base/'model.int8.onnx'),
                            tokens=str(base/'tokens.txt'),num_threads=4,use_itn=True,language=lang)
                    rec=recognizers[lang];stream=rec.create_stream();stream.accept_waveform(rate,pcm.astype(np.float32)/32768);rec.decode_stream(stream)
                    chars=[(c,float(t)) for token,t in zip(stream.result.tokens,stream.result.timestamps) for c in token if '\u4e00'<=c<='\u9fff']
                    marks=[i for i in range(len(chars)-1) if chars[i][0]=='今' and chars[i+1][0]=='日']
                    if len(marks)!=1 or marks[0]<1:raise ValueError('negative_carrier_alignment_uncertain')
                    j=marks[0];lo=int((chars[j-1][1]+.18)*rate);hi=int((chars[j][1]-.10)*rate)
                    if hi<=lo+320:raise ValueError('negative_carrier_no_gap')
                    positions=np.arange(lo,min(hi,len(pcm)-160),80)
                    energy=[np.mean(pcm[k:k+160].astype(float)**2) for k in positions]
                    if not len(energy):raise ValueError('negative_carrier_empty_gap')
                    crop_end=int(positions[np.argmin(energy)])+160
                    row['negative_carrier_asr']=stream.result.text
                retained=pcm[:crop_end];bounds=interval(retained)
                row.update(bounds)
                row.update(endpoint_schema=1,sampling_group='positive' if row['label'] else 'hard_negative',
                    boundary_config=CONFIG,original_annotation_end=original.get('wake_end_sample'))
                # Keep all retained PCM, including onset and trailing context.
                if crop_end<len(pcm):
                    destination=args.out/(row['clip_id']+'.wav');sf.write(destination,retained,rate,subtype='PCM_16')
                    row.update(path=destination.resolve().relative_to(ROOT).as_posix(),source_pcm_sha256=row['pcm_sha256'],
                        pcm_sha256=hashlib.sha256(retained.astype('<i2').tobytes()).hexdigest(),
                        parent_clip_id=row['clip_id'],negative_crop_end=crop_end)
                if row['label']:
                    row.update(wake_start_sample=bounds['speech_start_sample'],wake_end_sample=bounds['speech_end_high_sample'],
                        target_end_low_sample=bounds['speech_end_low_sample'],target_end_high_sample=bounds['speech_end_high_sample'])
                place(row,retained)
                accepted.append(row)
            except ValueError as error:
                rejected.append(dict(clip_id=row['clip_id'],split=row['split'],language=row['language'],label=row['label'],reason=str(error)))
    (args.out/'manifest.jsonl').write_text(''.join(json.dumps(r,ensure_ascii=False)+'\n' for r in accepted),encoding='utf8')
    counts=Counter(f"{r['split']}/{r['language']}/{r['label']}" for r in accepted)
    report=dict(config=CONFIG,accepted=len(accepted),rejected=len(rejected),counts=dict(counts),rejections=rejected,retired_test_rows=retired,
        inputs=[dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in args.manifests],
        limitations='Automatic energy/CTC boundaries; no independent human phonetic labels.')
    (args.out/'audit.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('rejections','retired_test_rows')},indent=2,ensure_ascii=False))


if __name__=='__main__':main()
