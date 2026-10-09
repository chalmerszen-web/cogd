"""Package the installed clock app, exact source snapshot and verification."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from kws.flash_guard import validate_application
BUILD=ROOT/'build-clock'
EVIDENCE=ROOT/'artifacts/clock-20261010'
VERSION='0.12.4-clock'
SUFFIX=VERSION.removesuffix('-clock')

def sha(data):return hashlib.sha256(data).hexdigest()
def save(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

def main():
    app=(BUILD/'esp_hi_agent.bin').read_bytes();validate_application(app)
    assert app[48:80].split(b'\0')[0].decode()==VERSION
    flash=json.loads((EVIDENCE/f'flash-{SUFFIX}.log').read_text('utf-8-sig'))
    device=json.loads((EVIDENCE/f'device-test-{SUFFIX}/result.json').read_text('utf8'))
    request=json.loads((EVIDENCE/'request-run/result.json').read_text('utf8'))
    assert flash['installed'] and flash['application_verified'] and flash['data_partitions_unchanged']
    assert sha(app)==flash['new_application_sha256'] and device['passed'] and request['passed']
    assert device['before']['version']==VERSION and device['automatic_boot_clock']['dc_output_enabled']
    observation=EVIDENCE/f'user-observation-{SUFFIX}.json'
    visual=json.loads(observation.read_text('utf8')) if observation.exists() else {'confirmed':False,'status':'PENDING'}
    assert visual.get('firmware',VERSION)==VERSION
    verification={'firmware':VERSION,'ready_for_user_test':visual.get('status')!='FAIL',
        'application_sha256':sha(app),'application_verified':True,'data_partitions_unchanged':True,
        'usb_request_exact_match':True,'request_flash_unchanged':True,
        'host_tests_passed':4,'device_clock_test_passed':True,
        'automatic_boot_and_reboot_clock':True,'context_statistics_unchanged':device['context_before']==device['context_after'],
        'dc_output_enabled_between_transfers':True,
        'sclk_input_idle_pulldown':True,
        'visual_observation':visual,'serial_closed':device['serial_closed'],
        'backup_directory':flash['directory']}
    save(EVIDENCE/f'verification-{SUFFIX}.json',verification)
    release=ROOT/'firmware/releases'/VERSION;release.mkdir(parents=True,exist_ok=False)
    options={}
    for line in (BUILD/'CMakeCache.txt').read_text('utf8').splitlines():
        if line.startswith('AGENT_') and ':INTERNAL=' not in line:
            key,value=line.split('=',1);options[key.split(':')[0]]=value
    assert options['AGENT_CLOCK_BOOT']=='ON' and options['AGENT_USER_TEST']=='ON'
    options['AGENT_KWS_VERIFIED_DIR']='components/kws_c11/models/voice_test'
    names=subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=ROOT).decode().split('\0')
    roots={'boards','core','plugins','platform','main','components','cmake','third_party','tools','host_tests','tests'}
    top={'CMakeLists.txt','partitions.csv','dependencies.lock','sdkconfig.defaults','sdkconfig.upgrade.defaults',
         'sdkconfig.voice-test.defaults','sdkconfig.noaudio.defaults','README.md','SPEC.md','ACTIONLOG.md','.gitignore','.gitattributes'}
    docs={'docs/ESP_HI_IO_DRIVER_MANUAL.md','docs/USB_CLOCK_VALIDATION_REPORT.md','docs/HARDWARE_FACTS.md',
          'docs/HARDWARE_AUDIT.md','docs/pinmap.csv','docs/NATIVE_LED_RAM_REPORT.md','docs/CLOCK_LCD_REPAIR_REPORT.md'}
    files={}
    for name in sorted(set(names)-{''}):
        p=Path(name)
        if p.parts[0] not in roots and name not in top|docs and not name.startswith(('experiments/usb_clock/','experiments/lcd_probe_ram/','docs/reference/esp32c3/')) and name not in ('experiments/native_led_ram/start.S','experiments/native_led_ram/ram.ld','experiments/native_led_ram/native_led.c'):continue
        data=(ROOT/p).read_bytes()
        if re.search(rb'\bsk-[A-Za-z0-9_-]{24,}',data):raise ValueError('Credential-like literal in '+name)
        files[name]=data
    files['reproduce/sdkconfig']=(BUILD/'sdkconfig').read_bytes()
    files['artifacts/clock-20261010/clock-235958-preview.png']=(EVIDENCE/'clock-235958-preview.png').read_bytes()
    # Include observations and selected raw images, never private Flash backups.
    for name in ('docs/DISPLAY_SPEC.md','experiments/lcd_probe_ram/SPEC.md'):
        files[name]=(ROOT/name).read_bytes()
    evidence_names=[f'user-observation-{SUFFIX}.json',f'device-test-{SUFFIX}/result.json',
        f'flash-{SUFFIX}.log',f'host-tests-{SUFFIX}.log',
        'request-run/result.json','request-run/request.json','request-run/usb-rx.bin','02-codex-relay.json',
        'user-observation-0.12.3.json','camera/clock-0.12.3-after-cold-reboot.jpg',
        'lcd-phase-run/result.json','camera/clock-0.12.3-cold-observed.jpg',
        'hardware-source/bmgr-sources.json']
    evidence_names.extend(f'lcd-phase-run/phase-{i}.jpg' for i in range(8))
    evidence_names.extend(f'device-test-{SUFFIX}/'+c['file'] for c in device.get('camera_captures',[]))
    for name in evidence_names:
        if (EVIDENCE/name).is_file():
            files['artifacts/clock-20261010/'+name]=(EVIDENCE/name).read_bytes()
    files['reproduce/build-options.json']=(json.dumps(options,indent=2)+'\n').encode()
    hashes={name:{'bytes':len(data),'sha256':sha(data)} for name,data in files.items()}
    with zipfile.ZipFile(release/'source.zip','x',zipfile.ZIP_DEFLATED) as z:
        for name,data in files.items():z.writestr(name,data)
        z.writestr('reproduce/source-sha256.json',json.dumps(hashes,indent=2))
    with zipfile.ZipFile(release/'source.zip') as z:
        assert z.testzip() is None
        assert all(sha(z.read(name))==v['sha256'] for name,v in hashes.items())
    table=(BUILD/'partition_table/partition-table.bin').read_bytes()
    (release/'esp_hi_agent.bin').write_bytes(app);(release/'partition-table.bin').write_bytes(table)
    manifest={'firmware':VERSION,'phase':'clock','chip':'esp32c3','app_offset':65536,
        'accepted':bool(visual.get('confirmed')),
        'acceptance_scope':'USB device request to Codex and visible clock only',
        'release_status':'clock_validated' if visual.get('confirmed') else 'failed_visual_validation' if visual.get('status')=='FAIL' else 'awaiting_visual_confirmation',
        'files':{name:{'bytes':len(data),'sha256':sha(data)} for name,data in [('esp_hi_agent.bin',app),('partition-table.bin',table)]},
        'source_sha256':sha((release/'source.zip').read_bytes()),'source_files_verified':len(files),
        'build_command':'tools/build_clock.ps1','idf_commit':'fff9895c82d744c7237be8847347bdd1b07c6643',
        'history_budget_bytes':204800,'evidence':'docs/CLOCK_LCD_REPAIR_REPORT.md',
        'limitations':['Physical LCD controller identity remains unverified.','No external RTC; cold boot needs time synchronization.',
                       'Existing voice quality limitations are unchanged.','No autonomous persistent USB-to-Codex bridge installed.']}
    save(release/'manifest.json',manifest);save(release/'verification.json',verification)
    readme=(f'# ESP-HI {VERSION}\n\n'
        '本包含应用、分区表校验副本、完整源码快照 source.zip、manifest.json 与 verification.json。'
        '解压 source.zip 后，从 docs/ESP_HI_IO_DRIVER_MANUAL.md 阅读 IO/驱动/芯片手册，'
        '从 docs/CLOCK_LCD_REPAIR_REPORT.md 阅读实物验收与原始照片索引。\n\n'
        '当前项目可运行 python tools/install_latest.py --check 只检查安装包；'
        '重新安装运行 python tools/install_latest.py --port COM5。'
        '安装器先完整备份并校验分区，再仅写应用并核对数据区域。'
        '独立使用源码快照时，先将本包应用、分区表和 manifest.json 放入 firmware/latest/。'
        '分区表仅用于核对，勿作为本次重写分区表的指令。\n\n'
        '构建入口为 tools/build_clock.ps1，所需 ESP-IDF 提交及选项见 manifest.json 和 '
        'source.zip 内 reproduce/。工具链本体需另行配置。'
        '冷断电后须联网校时；此次通过范围为 USB 请求到可见时钟。\n')
    (release/'README.md').write_text(readme,encoding='utf8')
    output=ROOT/'outputs'/('esp-hi-'+VERSION+'.zip');output.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED) as z:
        for p in release.iterdir():z.write(p,p.name)
    with zipfile.ZipFile(output) as z:assert z.testzip() is None
    # The latest installer now restores the version actually running on the board.
    latest=ROOT/'firmware/latest';latest.mkdir(exist_ok=True)
    for name in ('esp_hi_agent.bin','partition-table.bin','manifest.json','README.md'):
        shutil.copyfile(release/name,latest/name)
    print(json.dumps({'package':str(output),'application_sha256':sha(app),
        'source_files_verified':len(files),'visual_confirmed':bool(visual.get('confirmed'))},ensure_ascii=False,indent=2))

if __name__=='__main__':main()
