"""Archive three-round results, exact readback and an explicitly scoped package.

Failed acoustic gates stay FAILED even when build/readback and idle checks pass.
Never export the raw readback: its intervening NVS contains device credentials.
"""
import hashlib,json,re,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5'
T=P/'wake_training/20261101/three-rounds'
GROUPS=('nihao_zh','hi_zh','nihao_yue','hi_yue')
def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def write(path,data):path.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')
def functional_check_if_needed(quiet_voice):
    """One separate normal-volume functional run; never replaces quiet failures."""
    if quiet_voice['complete']:return None,None,None
    folder=T/'normal-volume-voice'
    if not folder.exists():
        command=[sys.executable,'-X','utf8',str(P/'tools/wake_continuous.py'),'--wake-gain','.35',
                 '--port','COM3','--out',str(folder),'--firmware','0.2.0-p4-wake4','--mode','fast',
                 '--max-attempts','1','--prompt-manifest',str(P/'fixtures/manifest.json'),'--prompt-id','knowledge',
                 '--prompt-gain','.35','--wake-manifest',str(T/'combined-test.jsonl'),
                 '--wake-groups','nihao_zh','hi_zh','hi_yue','nihao_yue']
        with (T/'normal-volume-voice.log').open('w',encoding='utf8') as log:
            subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=280,check=False)
    report=read(folder/'report.json');speaker_folder=T/'normal-volume-speaker-check'
    if report['complete'] and not speaker_folder.exists():
        with (T/'normal-volume-speaker-check.log').open('w',encoding='utf8') as log:
            subprocess.run([str(ROOT/'.local/tts-python/Scripts/python.exe'),'-X','utf8',str(P/'tools/wake_speaker_check.py'),
                            '--voice',str(folder),'--out',str(speaker_folder)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=120,check=False)
    speaker=read(speaker_folder/'report.json') if (speaker_folder/'report.json').exists() else None
    state_folder=T/'post-functional-state'
    if not state_folder.exists():
        with (T/'post-functional-state.log').open('w',encoding='utf8') as log:
            subprocess.run([sys.executable,'-X','utf8',str(P/'tools/wake_snapshot.py'),'--port','COM3',
                            '--out',str(state_folder),'--firmware','0.2.0-p4-wake4','--idle-seconds','30'],
                           cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=100,check=True)
    state=read(state_folder/'status.json');assert state['complete']
    return report,speaker,state
def main():
    selected=read(T/'selection.json');r=selected['selected_round'];build=T/f'candidate-r{r}-build'
    frozen=read(T/'final-frozen/report.json');state=read(T/'final-state/status.json')
    voice=read(T/'final-voice/report.json');pipeline=read(T/'pipeline-result.json')
    normal_voice,normal_speaker,post_state=functional_check_if_needed(voice)
    if post_state:state=post_state
    assert selected['complete'] and frozen['complete'] and state['complete']
    assert len(frozen['trials'])==84 and len(selected['rounds'])==3
    assert sha(build/'cogd_p4.bin')==selected['selected_app_sha256']==frozen['candidate_app_sha256']
    assert frozen['manifest_sha256']==selected['frozen_manifest_sha256']==sha(T/'frozen-stress/manifest.jsonl')
    rows=[json.loads(s) for s in (T/'frozen-stress/manifest.jsonl').read_text(encoding='utf8').splitlines()]
    assert [t['source'] for t in frozen['trials']]==rows
    assert state['wake']['state']=='listening' and state['voice']['voice_enabled'] and state['system']['wifi']
    assert state['hardware']['cpu_mhz']==200 and state['wake']['threshold']==740 and state['wake']['input_gain']==1
    sdk=(build/'sdkconfig').read_text(encoding='utf8')
    cache=(build/'CMakeCache.txt').read_text(encoding='utf8')
    assert re.search(r'^AGENT_KEYWORD_PCM:[^=]+=OFF$',cache,re.M)
    assert 'CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200' in sdk
    validation=read(T/f'validation-r{r}/validation.json')
    assert validation['complete'] and all(p['exact'] for p in validation['parity'])
    for trial in frozen['trials']:
        assert trial['after']['dma_lost']==trial['before']['dma_lost']
        assert trial['before']['threshold']==740 and trial['before']['input_gain']==1
        assert trial['playback_gain']==trial['source']['playback_gain']
    raw=(P/'.local/three-rounds-final-readback.bin').read_bytes();images=[]
    for name,offset in (('bootloader/bootloader.bin',0x2000),('partition_table/partition-table.bin',0x8000),('cogd_p4.bin',0x10000)):
        expected=(build/name).read_bytes();actual=raw[offset-0x2000:offset-0x2000+len(expected)]
        assert expected==actual,('Readback mismatch',name)
        assert (P/'build'/name).read_bytes()==expected
        images.append({'file':name,'offset':hex(offset),'bytes':len(expected),'sha256':sha(build/name),'readback_match':True})
    memory=read(T/f'memory-r{r}.json')
    assert memory['candidate']['elf_sha256']==sha(build/'cogd_p4.elf')
    heaps={t['before']['model_heap'] for t in frozen['trials']};workspaces={t['profile_before']['workspace'] for t in frozen['trials']}
    assert heaps=={state['wake']['model_heap']} and workspaces=={state['profile']['workspace']}
    speaker=read(T/'final-speaker-check/report.json') if (T/'final-speaker-check/report.json').exists() else None
    wake_pass=all(frozen['groups'][g]['triggers']/frozen['groups'][g]['trials']>=.9 for g in GROUPS) and frozen['groups']['negative']['triggers']==0
    dev=selected['rounds'][r-1]
    dev_pass=all(dev['groups'][g]['triggers']/dev['groups'][g]['trials']>=.9 for g in GROUPS) and dev['groups']['negative']['triggers']==0
    voice_pass=voice['complete'] and len(voice['trials'])==4 and voice['max_attempts_per_turn']==1
    speaker_pass=bool(speaker and speaker['complete'] and len(speaker['replies'])==4)
    accepted=all((wake_pass,dev_pass,voice_pass,speaker_pass))
    result={'complete':True,'status':'PASS_FIXED_SYNTHETIC_SCOPE' if accepted else 'FAILED_ACCEPTANCE_DEVELOPMENT_CANDIDATE',
            'selected_round':r,'app_sha256':selected['selected_app_sha256'],
            'gates':{'frozen_wakes':'PASS' if wake_pass else 'FAIL','development_wakes':'PASS' if dev_pass else 'FAIL',
                     'four_continuous_dialogues':'PASS' if voice_pass else 'FAIL',
                     'external_speaker':'PASS' if speaker_pass else 'FAIL' if speaker else 'NOT RUN',
                     'integer_parity':'PASS','flash_readback':'PASS','final_idle':'PASS','human_distance_test':'NOT RUN'},
            'baseline':read(T/'baseline-stress/report.json')['groups'],'rounds':selected['rounds'],
            'final_groups':frozen['groups'],'final_conditions':frozen['conditions'],
            'voice_turns':len(voice['trials']),'voice_error':voice.get('error'),
            'separate_normal_volume_functional_check':None if not normal_voice else {
                'gain':.35,'complete':normal_voice['complete'],'turns':len(normal_voice['trials']),
                'external_speaker':'PASS' if normal_speaker and normal_speaker['complete'] else 'FAIL' if normal_voice['complete'] else 'NOT RUN',
                'error':normal_voice.get('error'),'included_in_quiet_rates':False},
            'memory':{'model_heap_bytes':next(iter(heaps)),'workspace_bytes_included_in_heap':next(iter(workspaces)),
                      'model_constants_bytes':memory['candidate']['linked_constant_bytes'],
                      'constant_groups':memory['candidate']['groups'],
                      'constant_delta_from_original_bytes':memory['delta_bytes'],
                      'app_bytes':(build/'cogd_p4.bin').stat().st_size,
                      'app_delta_from_this_turn_baseline':(build/'cogd_p4.bin').stat().st_size-(T/'baseline-build/cogd_p4.bin').stat().st_size,
                      'max_observed_inference_us':max(t['profile_after']['max_us'] for t in frozen['trials']),
                      'storage':memory['storage']},
            'images':images,'selection_used_final_test':False,
            'scope':'28 synthetic source recordings, each in three prepared acoustic conditions, fixed physical speaker position. Mandarin voices disjoint; Cantonese recordings disjoint but voices known. No calibrated SPL, direction, distance or human-rate claim.',
            'frozen_manifest_sha256':sha(T/'frozen-stress/manifest.jsonl'),
            'readback_contains_nvs_and_not_exported':True,'pipeline':pipeline}
    write(T/'result.json',result)
    package=P/'firmware/experimental-wake4-three-rounds-20261101'
    archive=P/'evidence/wake4-three-rounds-20261101'
    package.mkdir(exist_ok=False);archive.mkdir(exist_ok=False)
    for row in images:
        target=package/row['file'];target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(build/row['file'],target)
    for name in ('sdkconfig','flasher_args.json'):shutil.copy2(build/name,package/name)
    shutil.copy2(P/'dependencies.lock',package/'dependencies.lock')
    manifest={'version':'0.2.0-p4-wake4','variant':f'three-rounds-r{r}','status':result['status'],
              'device':'ESP32-P4-WIFI6-Touch-LCD-3.5, 16 MB Flash, 200 MHz',
              'images':images,'gates':result['gates'],'evidence':'../../evidence/wake4-three-rounds-20261101/result.json'}
    write(package/'manifest.json',manifest)
    (package/'README.md').write_text('# 三轮优化候选固件\n\n这是当前设备所用的可回退候选，不包含 NVS 或服务凭据。验收状态：`'+result['status']+'`。完整失败和成功记录见 manifest.json / evidence。未达到门槛时不可称为正式验收版。\n\n在 cogd 根目录使用 `& .\\ESP32-P4-WIFI6-Touch-LCD-3.5\\tools\\flash.ps1 -Port COM3 -BuildDir firmware/experimental-wake4-three-rounds-20261101`。\n',encoding='utf8')
    selected_files=['result.json','selection.json','three-round-plan.json','frozen-test-plan.json','training-independence.json',
                    'skill-package.json','qwen31-content-selection.json','native-content-selection.json','pipeline-result.json','next-model-cost-estimate.json',
                    'baseline-stress/report.json','baseline-010/report.json','live-low-010/report.json',
                    'development-stress/manifest.jsonl','frozen-stress/manifest.jsonl','combined-train.jsonl','combined-test.jsonl',
                    'final-frozen/report.json','final-frozen/serial.log','final-voice/report.json','final-voice/serial.log',
                    'final-state/status.json','final-state/serial.log','final-readback.log','wake_matrix-r1-r2.py',
                    'check-round.ps1','final-check.ps1']
    if speaker:selected_files+=['final-speaker-check/report.json']
    if normal_voice:
        selected_files+=['normal-volume-voice/report.json','normal-volume-voice/serial.log',
                         'post-functional-state/status.json','post-functional-state/serial.log']
    if normal_speaker:selected_files+=['normal-volume-speaker-check/report.json']
    for i in (1,2,3):
        selected_files += [f'acoustic-r{i}/report.json',f'acoustic-r{i}/serial.log',f'validation-r{i}/validation.json',
                           f'train-r{i}/training-report.json',f'train-r{i}/config.json',f'features-r{i}/features.json',
                           f'memory-r{i}.json',f'flash-r{i}.log']
    for name in selected_files:
        source=T/name
        assert source.is_file(),source
        target=archive/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    provenance=read(P/'models/wake4/provenance.json')
    provenance['status']=result['status'];provenance['three_rounds_result']={'path':(archive/'result.json').relative_to(ROOT).as_posix(),'sha256':sha(archive/'result.json')}
    write(P/'models/wake4/provenance.json',provenance)
    shutil.copytree(P/'models/wake4',archive/'selected-models')
    # Refresh the restored sources' mtime so the next build cannot reuse objects
    # belonging to a different candidate, even when selecting an earlier round.
    for file in (P/'models/wake4').iterdir():
        if file.is_file():file.touch()
    shutil.copytree(P/'tools',archive/'tools',ignore=shutil.ignore_patterns('__pycache__'))
    secret=Path(r'C:\Users\PC\Desktop\adev.key.txt').read_text(encoding='utf-8-sig').strip().encode()
    for folder in (archive,package):
        files={}
        for file in sorted(folder.rglob('*')):
            if file.is_file():
                assert secret not in file.read_bytes()
                files[file.relative_to(folder).as_posix()]=sha(file)
        write(folder/'SHA256.json',files)
        assert all(sha(folder/name)==digest for name,digest in files.items())
    print(json.dumps(result,ensure_ascii=False))
if __name__=='__main__':main()
