"""Generate frozen native Cantonese recordings with explicit source identity.

The original split describes round one. After its failure, both native voices
were retired into TRAIN. Fresh round two holds out recordings, not speakers.

Uses the installed Qianwen skill. Recover each successful request exactly once;
an OSS fake-IP DNS result is handled by pinning a verified public DoH A record
for that one credential-free, certificate-verified download only.
"""
import argparse,hashlib,ipaddress,json,subprocess,sys,time,urllib.parse,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/wake_training/20261101'
PRIVATE=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/.local/wake4-qwen-tts'
SKILL=Path.home()/'.codex/skills/qianwen-model-suite/scripts/qianwen.py'
sys.path.insert(0,str(ROOT/'training/tts'))
from generate import save_wave

_dns_cache=None
def recover(state,path):
    global _dns_cache
    result=json.loads((state/'result.private.json').read_text(encoding='utf8'))
    parts=urllib.parse.urlsplit(result['output']['audio']['url'])
    assert parts.hostname=='dashscope-result-bj.oss-cn-beijing.aliyuncs.com' and not parts.username and parts.port is None
    if _dns_cache and time.monotonic()-_dns_cache[0]<180:
        dns=_dns_cache[1]
    else:
        for attempt in range(3):
            try:
                with urllib.request.urlopen('https://dns.google/resolve?name='+parts.hostname+'&type=A',timeout=15) as r:dns=json.load(r)
                _dns_cache=(time.monotonic(),dns);break
            except (OSError,urllib.error.URLError):
                if attempt==2:raise
                time.sleep(1)
    ip=next(x['data'] for x in dns['Answer'] if x['type']==1);assert ipaddress.ip_address(ip).is_global
    url=urllib.parse.urlunsplit(('https',parts.netloc,parts.path,parts.query,''))
    subprocess.run(['curl.exe','--fail','--silent','--show-error','--retry','2','--retry-delay','1','--noproxy','*','--resolve',parts.hostname+':443:'+ip,
                    '--max-time','20','--output',str(path),url],check=True,capture_output=True)
    assert path.read_bytes()[:4]==b'RIFF'
    (state/'download-recovery.json').write_text(json.dumps({'request_id':result['request_id'],'host':parts.hostname,'public_dns':dns,
        'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'tls_verified':True,'api_resubmitted':False},indent=2),encoding='utf8')

def main():
    import soundfile as sf
    parser=argparse.ArgumentParser();parser.add_argument('--only-ids',nargs='+')
    modes=parser.add_mutually_exclusive_group()
    modes.add_argument('--fresh-round2',action='store_true',help='New recordings of known native voices; utterance holdout, never speaker-disjoint')
    modes.add_argument('--fresh-round3',action='store_true',help='Another frozen recording holdout of the two known native voices')
    modes.add_argument('--joint-train',action='store_true',help='New native TRAIN recordings; no held-out audio is consumed')
    args=parser.parse_args()
    folder='qwen-native-joint-train' if args.joint_train else 'qwen-native-round3' if args.fresh_round3 else 'qwen-native-round2' if args.fresh_round2 else 'qwen-native'
    out=BASE/folder;out.mkdir(exist_ok=True);PRIVATE.mkdir(exist_ok=True)
    texts=['Hi，小言。','嗨，小言。','Hi 小言！','嗨小言。','Hi，小言！','嗨，小言！','Hi 小言。','嗨小言！']
    jobs=[]
    for voice,split,count in [('Rocky','validation',4),('Kiki','test',8)]:
        for i,text in enumerate(texts[:count]+['你好，小言。','你好小言！']+(['你好，小言！','你好小言。'] if split=='test' else [])+['Hi。','小言。','Hi，小燕。']):
            negative=text in ('Hi。','小言。','Hi，小燕。')
            group='negative' if negative else 'nihao_yue' if text.startswith('你好') else 'hi_yue'
            jobs.append({'clip_id':f'p4-native-{voice.lower()}-{i:02d}','voice':voice,'source_group':'qwen3-system-'+voice,
                'split':split,'label':int(not negative),'wake_group':group,'language':'yue','text':text.rstrip('。！'),'source_text':text})
    if args.fresh_round2 or args.fresh_round3 or args.joint_train:
        jobs=[]
        marker='jt' if args.joint_train else 'r3' if args.fresh_round3 else 'r2'
        new_texts=texts[:6]*2+['Hi。','小言。','Hi，小燕。','Hi，小楊。'] if args.joint_train else texts[:6]+['Hi。','小言。','Hi，小燕。']
        positives=12 if args.joint_train else 6
        for voice in ('Rocky','Kiki'):
            for i,text in enumerate(new_texts):
                negative=i>=positives
                jobs.append({'clip_id':f'p4-native-{marker}-{voice.lower()}-{i:02d}','voice':voice,'source_group':'qwen3-system-'+voice,
                    'split':'train' if args.joint_train else 'test','label':int(not negative),'wake_group':'negative' if negative else 'hi_yue','language':'yue',
                    'text':text.rstrip('。！'),'source_text':text,'evaluation_scope':'TRAIN' if args.joint_train else 'new utterance of TRAIN voice; not speaker-disjoint'})
    frozen=out/'jobs.json';content=json.dumps(jobs,ensure_ascii=False,indent=2)+'\n'
    if frozen.exists():assert frozen.read_text(encoding='utf8')==content
    else:frozen.write_text(content,encoding='utf8')
    manifest=out/'manifest.jsonl';done={r['clip_id'] for r in map(json.loads,manifest.read_text(encoding='utf8').splitlines())} if manifest.exists() else set()
    for row in jobs:
        ident=row['clip_id']
        if args.only_ids and ident not in args.only_ids:continue
        if ident in done:continue
        task=PRIVATE/(ident+'.json');state=PRIVATE/ident;raw=PRIVATE/(ident+'.wav')
        with (PRIVATE/(ident+'.log')).open('a',encoding='utf8') as log:
            if not task.exists():
                subprocess.run([sys.executable,str(SKILL),'template','tts-qwen','--output',str(task)],check=True,stdout=log,stderr=subprocess.STDOUT)
                request=json.loads(task.read_text(encoding='utf8'));request['request']['input']={'text':row['source_text'],'voice':row['voice']}
                task.write_text(json.dumps(request,ensure_ascii=False,indent=2),encoding='utf8')
            subprocess.run([sys.executable,str(SKILL),'plan','--task',str(task)],check=True,stdout=log,stderr=subprocess.STDOUT)
            subprocess.run([sys.executable,str(SKILL),'run','--task',str(task),'--state-dir',str(state),'--execute'],check=True,stdout=log,stderr=subprocess.STDOUT)
            receipt=json.loads((state/'state.json').read_text(encoding='utf8'));assert receipt['status']=='succeeded'
            result=subprocess.run([sys.executable,str(SKILL),'collect','--state-dir',str(state)],stdout=log,stderr=subprocess.STDOUT)
        if not raw.exists():
            # The successful API result is reused, never re-created to recover bytes.
            recover(state,raw)
        samples,rate=sf.read(raw,dtype='float32');path=out/(ident+'.wav');pcm=save_wave(path,samples,rate)
        record=dict(row,path=path.relative_to(ROOT).as_posix(),seconds=len(pcm)/16000,wake_start_sample=0,wake_end_sample=len(pcm),
            original_recording_id=ident,reference_speaker_id=row['source_group'],pcm_sha256=hashlib.sha256(pcm.tobytes()).hexdigest(),
            wav_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),license_id='Qwen-API-generated-audio',tts_family='Qwen3-TTS-Flash',
            request_id=receipt['request_id'],annotation='Native Cantonese preset; automatic pronunciation screening still required')
        with manifest.open('a',encoding='utf8') as f:f.write(json.dumps(record,ensure_ascii=False)+'\n')
        print(json.dumps({'id':ident,'split':row['split'],'seconds':record['seconds']}),flush=True)

if __name__=='__main__':main()
