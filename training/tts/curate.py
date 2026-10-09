"""Conservative ASR screening/alignment before KWS training; failures retained."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import numpy as np
import soundfile as sf
import sherpa_onnx

ROOT=Path(__file__).resolve().parents[2]
# Exact pronunciations, not arbitrary fuzzy edit distance. Cantonese 言/賢 are
# jin4 (CUHK Lexis); 燕(jin3), 嚴(jim4), 顏(ngaan4) are intentionally excluded.
ALIASES={'zh':set('言严嚴妍颜顏延研沿炎盐鹽岩'),'yue':set('言贤賢妍然燃延弦研')}

def compact(text):return re.sub(r'[^\u4e00-\u9fff]','',text)

def target_prefix(text,language):
    text=compact(text)
    prefixes=('你好小','你好少') if language=='yue' else ('你好小',)
    return len(text)>=4 and text[:3] in prefixes and text[3] in ALIASES[language]

def target_positions(text,language):
    text=compact(text)
    return [i for i in range(max(0,len(text)-3)) if target_prefix(text[i:i+4],language)]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('manifests',type=Path,nargs='+')
    ap.add_argument('--out',type=Path,required=True);args=ap.parse_args()
    args.out=args.out.resolve()
    args.out.mkdir(parents=True,exist_ok=False)
    base=ROOT/'artifacts/kws-phase2/asr'
    def make(language):return sherpa_onnx.OfflineRecognizer.from_sense_voice(model=str(base/'model.int8.onnx'),
        tokens=str(base/'tokens.txt'),num_threads=4,use_itn=True,language=language)
    recognizer=make('auto'); forced={}
    accepted=[];audit=[]
    for manifest in args.manifests:
        for line in manifest.read_text(encoding='utf8').splitlines():
            row=json.loads(line);samples,rate=sf.read(ROOT/row['path'],dtype='float32')
            if rate!=16000:raise ValueError('Unexpected sample rate')
            stream=recognizer.create_stream();stream.accept_waveform(rate,samples);recognizer.decode_stream(stream)
            result=stream.result;clean=compact(result.text)
            reason='mixed_script_transcript' if re.search(r'[A-Za-z]',result.text) else None
            positions=target_positions(result.text,row['language']);target_at=positions[0] if positions else None
            note=dict(clip_id=row['clip_id'],label=row['label'],split=row['split'],language=row['language'],
                source_text=row.get('source_text',row['text']),asr_text=result.text,asr_language=result.lang)
            if not row['label']:
                if positions:reason='negative_generated_target'
            else:
                if not positions:reason='target_phonemes_uncertain'
                elif len(positions)!=1:reason='ambiguous_target_span'
                elif result.lang!='<|'+row['language']+'|>':reason='language_uncertain'
                elif not row.get('carrier') and len(clean)!=4:reason='extra_words'
                else:
                    start=row['wake_start_sample'];end=row['wake_end_sample']
                    if row.get('carrier'):
                        characters=[(c,float(t)) for token,t in zip(result.tokens,result.timestamps)
                                    for c in compact(token)]
                        if len(characters)<target_at+5 or characters[target_at+4][0]!='今':reason='carrier_alignment_uncertain'
                        else:
                            # CTC timestamps approximate word positions. End in an
                            # observed energy trough after the last syllable and
                            # before the carrier; then re-recognize the crop.
                            start=max(0,int((characters[target_at][1]-.24)*rate))
                            lo=int((characters[target_at+3][1]+.18)*rate);hi=int((characters[target_at+4][1]-.10)*rate)
                            if hi<=lo+320:reason='no_safe_word_boundary'
                            else:
                                positions=np.arange(lo,min(hi,len(samples)-160),80)
                                energy=np.array([np.mean(samples[p:p+160]**2) for p in positions])
                                end=int(positions[np.argmin(energy)])+160
                    if reason is None:
                        word=samples[start:end]
                        if not 6400<=len(word)<=32768:reason='word_outside_duration_budget'
                        else:
                            language=row['language']
                            if language not in forced:forced[language]=make(language)
                            decoder=forced[language];check=decoder.create_stream()
                            check.accept_waveform(rate,word);decoder.decode_stream(check)
                            note['crop_asr']=check.result.text
                            if re.search(r'[A-Za-z]',check.result.text) or not target_prefix(check.result.text,language) or len(compact(check.result.text))!=4:
                                reason='cropped_word_not_confirmed'
                            else:
                                path=args.out/(row['clip_id']+'.wav')
                                pcm=np.clip(np.floor(word*32768+.5),-32768,32767).astype('<i2')
                                sf.write(path,pcm,rate,subtype='PCM_16')
                                row={**row,'source_pcm_sha256':row['pcm_sha256'],'path':path.relative_to(ROOT).as_posix(),
                                    'pcm_sha256':hashlib.sha256(pcm.tobytes()).hexdigest(),'parent_clip_id':row['clip_id'],
                                    'wake_start_sample':0,'wake_end_sample':len(pcm),'needs_alignment':False,
                                    'annotation':'ASR timestamps plus energy trough; isolated ASR screening, not human annotation',
                                    'crop_start':start,'crop_end':end,'receptive_field_exceeded':False}
            note['rejected_reason']=reason;audit.append(note)
            if reason is None:accepted.append(row)
            print(json.dumps(note,ensure_ascii=False),flush=True)
            if reason is None:
                with (args.out/'manifest.jsonl').open('a',encoding='utf8') as stream:stream.write(json.dumps(row,ensure_ascii=False)+'\n')
            with (args.out/'audit.jsonl').open('a',encoding='utf8') as stream:stream.write(json.dumps(note,ensure_ascii=False)+'\n')
    counts={}
    for row in accepted:
        key=f"{row['split']}/{row['language']}/{row['label']}";counts[key]=counts.get(key,0)+1
    (args.out/'summary.json').write_text(json.dumps(dict(total=len(audit),accepted=len(accepted),counts=counts,
        status='automatic screening only; human pronunciation remains unverified'),indent=2))

if __name__=='__main__':main()
