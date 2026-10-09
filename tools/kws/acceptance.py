"""Audit Phase 1 evidence. Refuse to mark missing or unfinished gates passed."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/kws-phase1'
def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def require(condition,message):
    if not condition:raise AssertionError(message)
def sha(data):return hashlib.sha256(data).hexdigest()

def main():
    subprocess.run([sys.executable,str(ROOT/'tools/kws/verify_sources.py')],check=True)
    subprocess.run([sys.executable,str(ROOT/'tools/kws/resource_report.py')],check=True)
    host=read(OUT/'host-parity.json');board=read(OUT/'board-parity.json')
    bench=read(OUT/'microphone-benchmark.json');extra=read(OUT/'extra-device-checks.json')
    regression=read(OUT/'agent-regression-accepted/report.json');resources=read(OUT/'resources.json')
    require(host['passed'] and host['torch_conv1d_integer_parity'],'Host arithmetic incomplete')
    require(host['integer_frames']>=4096 and host['integer_values']>=1085440,'Host coverage incomplete')
    require(board['complete'] and board['frames']==512 and not board['mismatches'],'Board parity incomplete')
    require(board['values']==176640,'Board trace coverage differs')
    require(bench['complete'] and bench['seconds']>=600,'Final live benchmark incomplete')
    require(bench['profile']['blocks']>=18500 and bench['profile']['max_us']<32000 and bench['p99_us_upper_bound']<=16000,'Realtime gate failed')
    require(bench['wake_after']['dma_lost']==bench['wake_before']['dma_lost']==0,'DMA loss')
    require(all(a['profile']['blocks']<=b['profile']['blocks'] for a,b in zip(bench['samples'],bench['samples'][1:])),'Inference counter reset')
    require(extra['complete'] and len(extra['reopen_cycles'])==20 and extra['timeout_released'],'Lifecycle/failure checks incomplete')
    require(29<=extra['timeout_seconds']<33,'Session timeout differs')
    require(extra['captured']['clip_ms']==2000 and extra['captured']['capture_error']=='ok' and extra['replayed']['play_error']=='ok','Capture/playback regression')
    require(extra['replayed']['mic_overruns']==0,'Microphone overrun')
    require(regression['complete'] and not regression['cleanup_errors'],'Agent regression incomplete')
    chats=[c for c in regression['commands'] if c['command'].startswith('agent chat ')]
    require(len(chats)==2 and all(c['terminal']=='@done' for c in chats),'Two cloud paths not complete')
    statuses=[r['status'] for r in bench['samples']]+[regression['after'],extra['after']]
    statuses.extend(s for c in chats for s in c['status_objects'])
    minimum=min(s['min_heap'] for s in statuses)
    require(minimum>=32768 and resources['listening_smallest_largest_block']>=24576,'Heap gates failed')
    workspace=bench['profile']['workspace']
    # Count the ENTIRE unchanged 4096-byte audio task stack, not just added use.
    own_static=resources['kws_archives']['esp-idf/agent_speech/libagent_speech.a']['total']-resources['kws_archives']['esp-idf/agent_speech/libagent_speech.a']['flash']
    require(workspace+own_static<=16384 and workspace+own_static+4096<=20480,'Workspace+stack gate failed')
    require(extra['replayed']['audio_stack']>0,'No observed stack margin')
    flash=read(OUT/'flash-result.log');backup=Path(flash['directory'])
    before=(backup/'flash-before.bin').read_bytes();after=(backup/'flash-after.bin').read_bytes()
    require(len(before)==len(after)==0x400000,'Full backup length')
    require(sha(before)==flash['backup_sha256'] and sha(after)==flash['after_sha256'],'Backup hashes')
    require(before[:0x10000]==after[:0x10000] and before[0x190000:]==after[0x190000:],'Data changed while flashing')
    app=(ROOT/'build-kws-probe/esp_hi_agent.bin').read_bytes()
    require(sha(app)==flash['new_application_sha256']==resources['application_sha256'],'Evidence firmware mismatch')
    require(after[0x10000:0x10000+len(app)]==app,'Application readback')
    for c in ('ctx','clip'):
        require(flash['partitions'][c]['size']=={'ctx':2097152,'clip':458752}[c],'Partition changed')
    require(regression['context_after']['history_budget']==131072,'Context budget reduced')
    # Raw serial log is checked as well as counters; a reset must not disappear
    # behind a later successful query. Boot is not expected within these tests.
    raw=(OUT/'all-serial.log').read_text(encoding='utf-8')
    require(not re.search(r'ESP-ROM:|Guru Meditation|assert failed|rst:0x|Task watchdog',raw),'Reset/panic in serial transcript')
    report=dict(complete=True,scope='Phase 1 computation/resources only; untrained, not recognition',
        firmware_sha256=sha(app),application_bytes=len(app),workspace_bytes=workspace,
        own_static_bytes=own_static,workspace_plus_entire_audio_stack=workspace+own_static+4096,
        all_observed_min_heap=minimum,live_blocks=bench['profile']['blocks'],max_us=bench['profile']['max_us'],
        p99_us_upper_bound=bench['p99_us_upper_bound'],audio_stack_free=extra['replayed']['audio_stack'],
        board_values=board['values'],host_values=host['integer_values'],reopen_cycles=20,
        model_trained=False,rollback_pending=True)
    restored_path=OUT/'rollback-state.json'
    if restored_path.exists():
        restored=read(restored_path)
        require(restored['complete'] and restored['context_preserved'] and restored['test_clip_preserved'],'Rollback state failed')
        require(restored['status']['version']=='0.6.3-context' and restored['wake']['model']=='wn9s_hilexin' and restored['wake']['state']=='listening','Usable baseline not restored')
        log=(OUT/'rollback.log').read_text(encoding='utf-8-sig')
        require('Verification successful (digest matched).' in log and 'Application installed and verified.' in log,'Rollback verification missing')
        require((ROOT/'docs/WAKE_XIAOYAN_PHASE1_REPORT.md').is_file(),'Delivery report missing')
        report.update(rollback_pending=False,delivery_complete=True,restored_version='0.6.3-context')
    (OUT/'acceptance.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
