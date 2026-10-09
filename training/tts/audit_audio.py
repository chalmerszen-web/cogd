"""Local ASR screening of synthesis; explicitly not human pronunciation proof."""
import argparse
import json
from pathlib import Path
import soundfile as sf
import sherpa_onnx

ROOT=Path(__file__).resolve().parents[2]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('manifests',type=Path,nargs='+')
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--language',choices=['auto','zh','yue'],default='auto');args=ap.parse_args()
    base=ROOT/'artifacts/kws-phase2/asr'
    recognizer=sherpa_onnx.OfflineRecognizer.from_sense_voice(model=str(base/'model.int8.onnx'),
        tokens=str(base/'tokens.txt'),num_threads=4,use_itn=True,language=args.language)
    results=[]
    for manifest in args.manifests:
        for line in manifest.read_text(encoding='utf8').splitlines():
            row=json.loads(line);samples,rate=sf.read(ROOT/row['path'],dtype='float32')
            stream=recognizer.create_stream();stream.accept_waveform(rate,samples);recognizer.decode_stream(stream)
            result=dict(clip_id=row['clip_id'],expected_language=row['language'],expected_text=row['text'],
                        asr_text=stream.result.text,asr_language=stream.result.lang,
                        samples=len(samples),receptive_field_exceeded=row.get('receptive_field_exceeded'),
                        tokens=list(stream.result.tokens),timestamps=list(stream.result.timestamps))
            results.append(result);print(json.dumps(result,ensure_ascii=False),flush=True)
    args.out.write_text(json.dumps(dict(method='local SenseVoice INT8 automatic screening; not human listening',language_setting=args.language,clips=results),ensure_ascii=False,indent=2),encoding='utf8')

if __name__=='__main__':main()
