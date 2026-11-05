"""Content-only screening; never reads keyword model scores or test outcomes."""
import argparse,hashlib,json,re,time
from pathlib import Path
import numpy as np,soundfile as sf,sherpa_onnx
ROOT=Path(__file__).resolve().parents[2]
B=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/wake_training/20261101/wide95'
def canonical(text):
    text=re.sub(r'[^a-z\u4e00-\u9fff]','',text.lower()).replace('嗨','hi')
    for alias in ('小严','小妍','小颜','小岩','小炎','晓言'):
        text=text.replace(alias,'小言')
    return text
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--watch-minutes',type=int,default=0);a=parser.parse_args()
    cache=B/'content-audit.jsonl';known={r['clip_id']:r for r in map(json.loads,cache.read_text(encoding='utf8').splitlines())} if cache.exists() else {}
    models=ROOT/'artifacts/kws-phase2/asr';decoders={};deadline=time.monotonic()+a.watch_minutes*60
    while True:
        rows=[]
        for i in range(4):
            path=B/f'corpus-{i}/manifest.jsonl'
            if path.exists():
                for line in path.read_text(encoding='utf8').splitlines():
                    try:rows.append(json.loads(line))
                    except json.JSONDecodeError:pass
        for row in rows:
            if row['clip_id'] in known:continue
            lang=row['language']
            if lang not in decoders:decoders[lang]=sherpa_onnx.OfflineRecognizer.from_sense_voice(model=str(models/'model.int8.onnx'),tokens=str(models/'tokens.txt'),num_threads=4,use_itn=True,language=lang)
            pcm,rate=sf.read(ROOT/row['path'],dtype='float32');stream=decoders[lang].create_stream();stream.accept_waveform(rate,np.r_[pcm,np.zeros(rate//2,dtype=np.float32)]);decoders[lang].decode_stream(stream)
            expected=canonical(row['text']);actual=canonical(stream.result.text)
            good=expected==actual and .25<=len(pcm)/rate<=4
            review=bool('小燕' in row['text'] or '小杨' in row['text'] or row['split']=='test')
            result={'clip_id':row['clip_id'],'wav_sha256':row['wav_sha256'],'transcript':stream.result.text,
                    'expected_canonical':expected,'actual_canonical':actual,'content_match':good,
                    'needs_blind_review':review,'language_hint':lang,'method':'Local SenseVoice; no expected text in recognition input'}
            with cache.open('a',encoding='utf8') as out:out.write(json.dumps(result,ensure_ascii=False)+'\n')
            known[row['clip_id']]=result
        for split in ('train','validation','test'):
            selected=[r for r in rows if r['split']==split and known[r['clip_id']]['content_match']]
            (B/(split+'-asr-screened.jsonl')).write_text(''.join(json.dumps(r,ensure_ascii=False)+'\n' for r in selected),encoding='utf8')
        print(json.dumps({'generated':len(rows),'audited':len(known),'content_match':sum(r['content_match'] for r in known.values())}),flush=True)
        if time.monotonic()>=deadline:break
        time.sleep(10)
if __name__=='__main__':main()
