"""Package a wake update only after the frozen physical test and voice gate pass."""
import argparse,datetime,hashlib,json,re,shutil
from pathlib import Path
P=Path(__file__).resolve().parents[1];ROOT=P.parent
VERSION='0.2.0-p4-wake4'

def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def write(path,data):path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

def main():
    p=argparse.ArgumentParser();p.add_argument('--matrix',type=Path,nargs=2,required=True)
    p.add_argument('--voice',type=Path,required=True);p.add_argument('--readback',type=Path,required=True)
    p.add_argument('--validation',type=Path,required=True);p.add_argument('--memory',type=Path,required=True)
    p.add_argument('--development',type=Path,required=True);p.add_argument('--state',type=Path,required=True)
    p.add_argument('--native-negative',type=Path,required=True);p.add_argument('--speaker',type=Path,required=True)
    p.add_argument('--test-manifest',type=Path,required=True);p.add_argument('--test-plan',type=Path,required=True)
    p.add_argument('--test-sha256',required=True);p.add_argument('--failed-round2',type=Path,required=True)
    p.add_argument('--independence-proof',type=Path,required=True);a=p.parse_args()
    a.matrix=[x.resolve() for x in a.matrix]
    for name in ('voice','readback','validation','memory','development','state','native_negative','speaker'):setattr(a,name,getattr(a,name).resolve())
    final=read(a.state);assert final['complete'] and final['hardware']['cpu_mhz']==200
    cache=(P/'build/CMakeCache.txt').read_text(encoding='utf8')
    assert re.search(r'^AGENT_KEYWORD_PCM:[^=]+=OFF$',cache,re.M),'Diagnostic build cannot be packaged'
    assert final['system']['version']==VERSION and final['voice']['voice_enabled'] and final['wake']['state']=='listening'
    frozen=a.test_manifest.resolve()
    frozen_sha=a.test_sha256
    assert sha(frozen)==frozen_sha
    plan=read(a.test_plan);assert plan['manifest_sha256']==frozen_sha
    independence=read(a.independence_proof)
    assert independence['complete'] and independence['manifest_sha256']==frozen_sha
    current_provenance=read(P/'models/wake4/provenance.json')
    training=read(P/current_provenance['training']/'config.json')
    assert independence['training_npz_sha256']==training['feature_hashes']['train.npz']
    frozen_rows=[json.loads(line) for line in frozen.read_text(encoding='utf8').splitlines()]
    expected_ids=[r['clip_id'] for r in frozen_rows]
    assert len(expected_ids)==len(set(expected_ids))==plan['source_count']
    groups={};hashes=set();gains=set();profile_max=0;heaps=set();workspaces=set()
    for folder in a.matrix:
        report=read(folder/'report.json');assert report['complete']
        assert report['before']['system']['version']==VERSION
        hashes.add(report['manifest_sha256'])
        gains.add(report['gain'])
        assert report['candidate_app_sha256']==sha(P/'build/cogd_p4.bin')
        assert [t['source']['clip_id'] for t in report['trials']]==expected_ids
        assert [t['source']['wav_sha256'] for t in report['trials']]==[r['wav_sha256'] for r in frozen_rows]
        for name,g in report['groups'].items():
            target=groups.setdefault(name,{'trials':0,'triggers':0})
            for k in target:target[k]+=g[k]
        for t in report['trials']:
            assert t['after']['dma_lost']==t['before']['dma_lost']
            assert t['before']['threshold']==740 and t['before']['input_gain']==1
            assert t['before']['model']=='xiaoyan_p4_wake4_24_24_48'
            heaps.add(t['before']['model_heap']);workspaces.add(t['profile_before']['workspace'])
            profile_max=max(profile_max,t['profile_after']['max_us'])
        assert not re.search(r'Guru Meditation|assert failed|ESP-ROM:',(folder/'serial.log').read_text(errors='replace'))
    assert hashes=={frozen_sha} and gains=={.35,.20}
    for group in ('nihao_zh','nihao_yue','hi_zh','hi_yue'):
        assert groups[group]['triggers']/groups[group]['trials']>=.9,(group,groups[group])
    assert groups['negative']['triggers']==0
    development=read(a.development/'report.json')
    assert development['candidate_app_sha256']==sha(P/'build/cogd_p4.bin')
    assert development['complete'] and development['groups']['negative']['triggers']==0
    assert all(development['groups'][g]['triggers']/development['groups'][g]['trials']>=.9 for g in ('nihao_zh','nihao_yue','hi_zh','hi_yue'))
    native=read(a.native_negative/'report.json')
    assert native['candidate_app_sha256']==sha(P/'build/cogd_p4.bin')
    assert native['complete'] and native['groups']['negative']['triggers']==0
    assert native['before']['system']['version']==VERSION
    voice=read(a.voice/'report.json');assert voice['complete'] and len(voice['trials'])==4
    assert set(voice['round_languages'])=={'nihao_zh','nihao_yue','hi_zh','hi_yue'}
    assert voice['max_attempts_per_turn']==1
    assert all(any('木星' in line and not line.startswith('@') for line in t['raw_lines']) for t in voice['trials'])
    speaker=read(a.speaker/'report.json');assert speaker['complete'] and len(speaker['replies'])==len(voice['trials'])
    assert speaker['source_sha256']==sha(a.voice/'speaker.wav') and all(t['contains_answer'] for t in speaker['replies'])
    validation=read(a.validation/'validation.json');assert validation['complete'] and all(r['exact'] for r in validation['parity'])
    data=a.readback.read_bytes();images=[]
    for name,offset in [('bootloader/bootloader.bin',0x2000),('partition_table/partition-table.bin',0x8000),('cogd_p4.bin',0x10000)]:
        image=(P/'build'/name).read_bytes();actual=data[offset-0x2000:offset-0x2000+len(image)]
        assert image==actual,('readback mismatch',name)
        images.append({'file':name,'offset':hex(offset),'bytes':len(image),'sha256':hashlib.sha256(image).hexdigest(),'readback_match':True})
    dest=P/'firmware'/VERSION;dest.mkdir(parents=True,exist_ok=False)
    provenance=read(P/'models/wake4/provenance.json')
    provenance['status']='ACCEPTED within frozen synthetic acoustic test scope; human validation pending'
    provenance['physical_acceptance']={'groups':groups,'test_manifest_sha256':frozen_sha,'group_scope':plan['group_scope'],
        'voice_turns':len(voice['trials']),'external_replies_verified':len(speaker['replies']),
        'app_sha256':next(row['sha256'] for row in images if row['file']=='cogd_p4.bin'),
        'human_validation':'NOT RUN'}
    write(P/'models/wake4/provenance.json',provenance)
    for row in images:
        target=dest/row['file'];target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(P/'build'/row['file'],target)
    for name in ('sdkconfig','flasher_args.json'):shutil.copy2(P/'build'/name,dest/name)
    shutil.copy2(P/'dependencies.lock',dest/'dependencies.lock')
    inputs={}
    for directory in ('main','core','plugins','boards/esp_hi','components/agent_speech','components/agent_vad','components/kws_c11',
                      'platform/espidf','cmake',P.name+'/board',P.name+'/components',P.name+'/models/wake4'):
        for f in (ROOT/directory).rglob('*'):
            if f.is_file() and f.suffix.lower() in ('.c','.h','.s','.inc','.cmake','.txt','.yml','.json'):
                inputs[f.relative_to(ROOT).as_posix()]=sha(f)
    for name in ('CMakeLists.txt',P.name+'/CMakeLists.txt',P.name+'/sdkconfig.defaults',P.name+'/tools/build.ps1',P.name+'/partitions.csv'):
        inputs[name]=sha(ROOT/name)
    write(dest/'source-sha256.json',inputs)
    manifest={'version':VERSION,'created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'board':P.name,'device_id':'esp-hi-e8f60aeb7a3f','runtime_cpu_mhz':200,'flash_bytes':16777216,
        'images':images,'frozen_manifest_sha256':next(iter(hashes)),'groups':groups,'voice_turns':len(voice['trials']),
        'model_heap_bytes':sorted(heaps),'workspace_bytes':sorted(workspaces),'maximum_kws_block_us':profile_max,
        'memory':read(a.memory),'development_groups':development['groups'],
        'supplemental_native_negative':native['groups']['negative'],
        'supplemental_native_negative_scope':'Regression on source identities and clips used in TRAIN',
        'group_scope':plan['group_scope'],
        'independence_check':independence,
        'failed_round1':read(P/'wake_training/20261101/round1-result-and-retirement.json')['original_test_groups'],
        'failed_round2':read(a.failed_round2)['groups'],
        'round2_training_retirement':read(P/'wake_training/20261101/round2-training-retirement.json'),
        'final_state':str(a.state.relative_to(ROOT)),
        'evidence':{'matrix':[str(x.relative_to(ROOT)) for x in a.matrix],'voice':str(a.voice.relative_to(ROOT)),
                    'quantized_validation':str(a.validation.relative_to(ROOT)),'external_speaker_check':str(a.speaker.relative_to(ROOT))},
        'scope':'Fixed synthetic playback through real board microphone at two host gains, same placement; not independent human trials or measured-distance acceptance',
        'human_validation':'NOT RUN; user reports quiet room at 20-50 cm',
        'limitation':'Rounds one and two were explicitly retired into TRAIN after failed physical tests; their later replays are training regression only. Current per-group source independence is recorded in group_scope. Native Rocky/Kiki identities occur in TRAIN; only their new recordings are held out. Broad noise, long-duration false wake, human dialect coverage and a universal maximum recall are not established.'}
    write(dest/'manifest.json',manifest)
    print(json.dumps({'package':str(dest),'groups':groups,'heap':sorted(heaps),'workspace':sorted(workspaces),'max_us':profile_max},ensure_ascii=False))

if __name__=='__main__':main()
