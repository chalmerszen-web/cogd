"""Download a bounded, pinned local TTS bundle; no inference or private upload."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import urllib.request

MODELS = {
    'mandarin': ('FunAudioLLM/CosyVoice2-0.5B', 'eec1ae6c79877dbd9379285cf8789c9e0879293d'),
    'cantonese': ('ASLP-lab/Cosyvoice2-Yue', 'b6d0eb0b4b594c67100e8786fe97227042d451a9'),
    'references': ('hexgrad/Kokoro-82M', 'f3ff3571791e39611d31c381e3a41a3af07b4987'),
}
COSY_FILES = ['README.md', 'cosyvoice2.yaml', 'campplus.onnx',
              'speech_tokenizer_v2.onnx', 'llm.pt', 'flow.pt', 'hift.pt',
              'CosyVoice-BlankEN/config.json', 'CosyVoice-BlankEN/model.safetensors',
              'CosyVoice-BlankEN/generation_config.json', 'CosyVoice-BlankEN/merges.txt',
              'CosyVoice-BlankEN/tokenizer_config.json', 'CosyVoice-BlankEN/vocab.json']
VOICES = ['zf_xiaobei','zf_xiaoni','zf_xiaoxiao','zf_xiaoyi',
          'zm_yunjian','zm_yunxi','zm_yunxia','zm_yunyang']

def fetch(url, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        temporary = path.with_name(path.name + '.part')
        with urllib.request.urlopen(url, timeout=90) as response, temporary.open('wb') as stream:
            shutil.copyfileobj(response, stream, 1024*1024)
        temporary.replace(path)
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''): digest.update(chunk)
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest.hexdigest(), url=url)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path,default=Path('artifacts/kws-phase2/tts'))
    parser.add_argument('--reserved-voices',action='store_true',help='Also download the final held-out af_heart/am_michael identities')
    args=parser.parse_args(); args.out.mkdir(parents=True,exist_ok=True)
    rows=[]
    for language,(repo,revision) in MODELS.items():
        api=json.load(urllib.request.urlopen(f'https://huggingface.co/api/models/{repo}/revision/{revision}',timeout=30))
        if api['sha']!=revision or api.get('cardData',{}).get('license')!='apache-2.0':
            raise ValueError('Model revision/license mismatch: '+repo)
        (args.out/(language+'-metadata.json')).write_text(json.dumps(api,ensure_ascii=False,indent=2),encoding='utf8')
        voices=VOICES+(['af_heart','am_michael'] if args.reserved_voices else [])
        files=(['README.md','VOICES.md','config.json','kokoro-v1_0.pth']+
               ['voices/'+v+'.pt' for v in voices]) if language=='references' else COSY_FILES
        jobs=[(f'https://huggingface.co/{repo}/resolve/{revision}/{name}',args.out/language/name) for name in files]
        with ThreadPoolExecutor(max_workers=3) as pool:
            for item in pool.map(lambda job: fetch(*job),jobs):
                item.update(repo=repo,revision=revision,license='Apache-2.0'); rows.append(item)
                print(json.dumps(item),flush=True)
                (args.out/'sources.lock.json').write_text(json.dumps(rows,indent=2),encoding='utf8')
    print('DOWNLOAD_COMPLETE',flush=True)

if __name__=='__main__': main()
