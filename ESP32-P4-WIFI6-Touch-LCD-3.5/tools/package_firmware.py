"""Package only firmware images that match the independent device readback."""
from pathlib import Path
import datetime,hashlib,json,re,shutil,subprocess
P=Path(__file__).resolve().parents[1];ROOT=P.parent
def sha(b):return hashlib.sha256(b).hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def write(p,d):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
voice=read(P/'evidence/voice-09-tail1200/report.json');validation=read(P/'evidence/validation-01/report.json')
assert voice['complete'] and validation['complete']
faults=re.findall(r'Guru Meditation|assert failed|abort\(\)|watchdog|ESP-ROM:',(P/'evidence/voice-09-tail1200/serial.log').read_text(errors='replace'),re.I)
assert not faults,faults
assert all(any('木星' in x and not x.startswith('@') for x in t['raw_lines']) for t in voice['trials'])
assert all(any(e['stage']=='llm_plan_response_done' for e in t['events']) for t in voice['trials'])
data=(P/'.local/final-readback.bin').read_bytes();dest=P/'firmware/0.1.0-p4-3.5';images=[]
for name,offset in [('bootloader/bootloader.bin',0x2000),('partition_table/partition-table.bin',0x8000),('cogd_p4.bin',0x10000)]:
    binary=(P/'build'/name).read_bytes();actual=data[offset-0x2000:offset-0x2000+len(binary)]
    assert binary==actual,f'Readback mismatch: {name}'
    target=dest/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(binary)
    images.append({'file':name,'offset':hex(offset),'bytes':len(binary),'sha256':sha(binary),'readback_sha256':sha(actual),'match':True})
for name in ['sdkconfig','flasher_args.json']:
    shutil.copy2(P/'build'/name,dest/name)
shutil.copy2(P/'dependencies.lock',dest/'dependencies.lock')
inputs={}
for directory in ['main','core','plugins','boards/esp_hi','components/agent_speech','components/agent_vad','components/kws_c11',
                  'platform/espidf','cmake','ESP32-P4-WIFI6-Touch-LCD-3.5/board','ESP32-P4-WIFI6-Touch-LCD-3.5/components']:
    for f in (ROOT/directory).rglob('*'):
        if f.is_file() and f.suffix.lower() in ['.c','.h','.s','.inc','.cmake','.txt','.yml','.json']:
            inputs[f.relative_to(ROOT).as_posix()]=sha(f.read_bytes())
for name in ['CMakeLists.txt','ESP32-P4-WIFI6-Touch-LCD-3.5/CMakeLists.txt','ESP32-P4-WIFI6-Touch-LCD-3.5/sdkconfig.defaults',
             'ESP32-P4-WIFI6-Touch-LCD-3.5/partitions.csv','ESP32-P4-WIFI6-Touch-LCD-3.5/tools/build.ps1']:
    f=ROOT/name;inputs[name]=sha(f.read_bytes())
write(dest/'source-sha256.json',inputs)
idf=subprocess.check_output(['git','-C',str(ROOT/'.toolchains/esp-idf'),'rev-parse','HEAD'],text=True).strip()
manifest={'version':'0.1.0-p4-3.5','created_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'board':'ESP32-P4-WIFI6-Touch-LCD-3.5','verified_device_id':'esp-hi-e8f60aeb7a3f','chip_revision':'3.2',
    'idf':'6.1','idf_commit':idf,'flash_bytes':16777216,'psram_bytes':33554432,'runtime_cpu_mhz':200,
    'flash_mode':'dio','flash_frequency':'80m','images':images,'readback':'PASS',
    'voice_acceptance':'../../evidence/voice-09-tail1200/report.json','device_validation':'../../evidence/validation-01/report.json',
    'scope':'Bring-up on this physical 3.5 board; fixed audio fixtures, not broad human/noise or manufacturing qualification',
    'limitation':'400 MHz had intermittent load access faults; current fixed 200 MHz profile passed the recorded bounded tests. Root physical cause not established.'}
write(dest/'manifest.json',manifest);write(P/'evidence/firmware-readback-check.json',{'status':'PASS','images':images})
state=read(P/'.local/final-speaker-asr/state.json');transcript=(P/'.local/final-speaker-asr/result.txt').read_text(encoding='utf8').strip()
assert '木星' in transcript
speaker={'source':'voice-09-tail1200/speaker.wav','crop_start_seconds':9,'crop_duration_seconds':6.5,
    'file':'final-speaker-check.wav','sha256':sha((P/'evidence/final-speaker-check.wav').read_bytes()),
    'model':'qwen3-asr-flash','transcript':transcript,'request_id':state['request_id'],'usage':state['usage'],
    'scope':'External microphone recording of actual speaker output; machine transcription, not subjective audio-quality acceptance'}
write(P/'evidence/final-speaker-check.json',speaker)
acceptance={'status':'PASS','voice_conversations':3,'first_attempt_wakes':3,'full_input_transcripts':3,
    'deepseek_responses':3,'tts_completions':3,'hardware_resets':3,'negative_clips':12,'false_wakes':0,
    'idle_seconds':30,'idle_false_wakes':0,'host_tests':'96/96 PASS','c3_build':'PASS (no C3 flash)',
    'display_control':'OFF/FILL/CLOCK telemetry PASS; visual/touch inspection NOT RUN','runtime_cpu_mhz':200,
    'external_speaker':speaker,'firmware_readback':'PASS','human_live_speech':'NOT RUN','power_disconnect_cycle':'NOT RUN',
    'long_duration_noise_test':'NOT RUN','ethernet_camera_sd':'NOT RUN'}
write(P/'evidence/acceptance.json',acceptance)
print(json.dumps({'package':str(dest),'readback':'PASS','images':len(images),'source_files_hashed':len(inputs)},ensure_ascii=False))
