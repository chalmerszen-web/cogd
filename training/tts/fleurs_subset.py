"""Bounded download of original FLEURS audio; no execution of dataset code."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import tarfile
import time
import urllib.request
import numpy as np
import soundfile as sf

ROOT=Path(__file__).resolve().parents[2]
REV='70bb2e84b976b7e960aa89f1c648e09c59f894dd'
BASE=f'https://huggingface.co/datasets/google/fleurs/resolve/{REV}/'

def load_exclusions(paths):
    """Exclude original recordings and exact PCM across every existing split."""
    ids=set(); hashes=set(); sources=[]
    for path in paths:
        raw=path.read_bytes()
        sources.append(dict(path=str(path),sha256=hashlib.sha256(raw).hexdigest()))
        for line in raw.decode('utf8').splitlines():
            if not line.strip():continue
            row=json.loads(line)
            if row.get('source_dataset')!='google/fleurs':continue
            ids.add(row['original_recording_id']); hashes.add(row['pcm_sha256'])
    return ids,hashes,sources

def possible_wake_text(text):
    compact=''.join(c for c in text if c.isalnum())
    return any(s in compact for s in ('小言','小嚴','小严','小妍'))

def decode_pcm(source):
    # FLEURS includes floating-point WAV. Asking libsndfile directly for int16
    # rounds normalized float samples to -1/0/1 instead of scaling them.
    samples,rate=sf.read(io.BytesIO(source),dtype='float32')
    if rate!=16000 or samples.ndim!=1 or not np.isfinite(samples).all():
        raise ValueError('Unexpected FLEURS audio format/values')
    pcm=np.clip(np.floor(samples.astype(np.float64)*32768+.5),-32768,32767).astype('<i2')
    if not len(pcm) or np.max(np.abs(pcm.astype(np.int32)))<32:
        raise ValueError('Speech source is effectively silent after conversion')
    return pcm,rate

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--count',type=int,default=160)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--splits',nargs='+',choices=('train','test'),default=['train','test'])
    parser.add_argument('--exclude-manifests',type=Path,nargs='*',default=[])
    parser.add_argument('--max-seconds',type=int,default=300)
    args=parser.parse_args()
    if not 1<=args.count<=1024 or not 1<=args.max_seconds<=600:parser.error('Download budget out of range')
    if len(set(args.splits))!=len(args.splits):parser.error('Duplicate split')
    excluded_ids,excluded_hashes,excluded_sources=load_exclusions(args.exclude_manifests)
    started=time.monotonic()
    def budget():
        if time.monotonic()-started>args.max_seconds:raise TimeoutError('Declared download budget exhausted')
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    (out/'plan.json').write_text(json.dumps(dict(revision=REV,count_per_language=args.count,
        splits=args.splits,max_seconds=args.max_seconds,exclusions=excluded_sources),indent=2)+'\n',encoding='utf8')
    originals=out/'originals';originals.mkdir()
    readme=urllib.request.urlopen(BASE+'README.md',timeout=20).read()
    if b'cc-by-4.0' not in readme.lower():raise ValueError('Pinned license could not be verified')
    (out/'README.upstream.md').write_bytes(readme)
    rows=[]
    # Upstream explicitly separates train speakers from dev/test. We do not
    # assert dev and test have disjoint speakers, and therefore do not use dev.
    for language,config in [('zh','cmn_hans_cn'),('yue','yue_hant_hk')]:
        for split in args.splits:
            budget()
            url=BASE+f'data/{config}/{split}.tsv'
            raw=urllib.request.urlopen(url,timeout=20).read();(out/(config+'-'+split+'.tsv')).write_bytes(raw)
            metadata={r[1]:r for r in csv.reader(io.StringIO(raw.decode()),delimiter='\t')}
            url=BASE+f'data/{config}/audio/{split}.tar.gz';count=0
            with urllib.request.urlopen(url,timeout=20) as response,tarfile.open(fileobj=response,mode='r|gz') as archive:
                for entry in archive:
                    budget()
                    name=Path(entry.name).name
                    if not entry.isfile() or name not in metadata:continue
                    ident=f'fleurs-{config}-{split}-{Path(name).stem}'
                    if ident in excluded_ids:continue
                    if not 0<entry.size<=10*1024*1024:raise ValueError('Source entry outside audio size budget')
                    source=archive.extractfile(entry).read();meta=metadata[name]
                    # Exclude literal target/homophone candidate text; do not
                    # relabel a possible wake phrase as background.
                    transcript=' '.join(meta[2:4])
                    if possible_wake_text(transcript):continue
                    pcm,rate=decode_pcm(source)
                    pcm_hash=hashlib.sha256(pcm.astype('<i2').tobytes()).hexdigest()
                    if pcm_hash in excluded_hashes:continue
                    excluded_ids.add(ident);excluded_hashes.add(pcm_hash)
                    original=originals/(ident+'.wav');original.write_bytes(source)
                    path=out/(ident+'.wav');sf.write(path,pcm,rate,subtype='PCM_16')
                    rows.append(dict(clip_id=ident,path=path.relative_to(ROOT).as_posix(),label=0,language=language,
                        source_group=f'fleurs-{config}-{split}-speaker-group-unexposed',original_recording_id=ident,
                        pcm_sha256=pcm_hash,license_id='CC-BY-4.0',
                        split=split,source_dataset='google/fleurs',revision=REV,source_url=url,
                        original_sha256=hashlib.sha256(source).hexdigest(),transcription=transcript,
                        original_path=original.relative_to(ROOT).as_posix(),
                        original_subtype=sf.info(io.BytesIO(source)).subtype,
                        converted_peak=int(np.max(np.abs(pcm.astype(np.int32)))),
                        seconds=len(pcm)/rate,speaker_separation='upstream train versus dev/test guarantee; individual identities unavailable'))
                    count+=1
                    if count>=args.count:break
            (out/'manifest.jsonl').write_text(''.join(json.dumps(r,ensure_ascii=False)+'\n' for r in rows),encoding='utf8')
            print(json.dumps(dict(language=language,split=split,clips=count)),flush=True)
            if count!=args.count:raise RuntimeError('Not enough distinct permitted recordings; do not fill with duplicates')
    (out/'ATTRIBUTION.txt').write_text('FLEURS: Google, Conneau et al., FLEURS: Few-shot Learning Evaluation of Universal Representations of Speech (2022).\nhttps://huggingface.co/datasets/google/fleurs\nCC BY 4.0: https://creativecommons.org/licenses/by/4.0/\nSubset converted to mono PCM16 WAV; originals identified in manifest.\n',encoding='utf8')

if __name__=='__main__':main()
