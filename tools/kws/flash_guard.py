"""Back up and verify a 4 MiB device; update only its existing application slot."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from datetime import datetime, timezone

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from migrate_context import partitions,application_size

def sha(data): return hashlib.sha256(data).hexdigest()
def require(condition,message):
    if not condition: raise ValueError(message)
def app_limit(protocol_diagnostic=False,kws_cross_diagnostic=False):
    # Normal budget stays1504KiB. A labelled protocol diagnostic can consume
    # at most1KiB of the32KiB reserve INSIDE the existing1536KiB app slot.
    require(not (protocol_diagnostic and kws_cross_diagnostic),'Diagnostic modes are mutually exclusive')
    # A dedicated, labelled KWS resource image can use8KiB of the same32KiB
    # app reserve. This never changes the partition or normal/protocol limit.
    return 1540096 + (8192 if kws_cross_diagnostic else 1024 if protocol_diagnostic else 0)
def validate_application(app,protocol_diagnostic=False,kws_cross_diagnostic=False):
    require(len(app)<=app_limit(protocol_diagnostic,kws_cross_diagnostic),'Oversized application')
    require(not protocol_diagnostic or b'-protocol-diag' in app,'Diagnostic application label missing')
    require(not kws_cross_diagnostic or b'-cross-budget' in app,'KWS resource application label missing')
    require(application_size(app)==len(app),'Invalid application')
def main():
    p=argparse.ArgumentParser();p.add_argument('--port',default='COM5');p.add_argument('--build',default='build-kws-probe');p.add_argument('--backup-only',action='store_true')
    p.add_argument('--phase',choices=['phase1','phase2','phase4','phase5','phase6','voice-cloud','voice-speed','voice-stream','voice-flow','clock'],default='phase1')
    diagnostic=p.add_mutually_exclusive_group()
    diagnostic.add_argument('--protocol-diagnostic',action='store_true',help='Allow up to1KiB of app reserve for a labelled protocol diagnostic; data layout unchanged')
    diagnostic.add_argument('--kws-cross-diagnostic',action='store_true',help='Allow up to8KiB of app reserve for a labelled KWS resource diagnostic; data layout unchanged')
    args=p.parse_args();build=ROOT/args.build
    app=(build/'esp_hi_agent.bin').read_bytes()
    validate_application(app,args.protocol_diagnostic,args.kws_cross_diagnostic)
    table=build/'partition_table/partition-table.bin'
    if not table.is_file():table=build/'partition-table.bin'  # Retained installation package.
    expected=partitions(table.read_bytes())
    require((expected['factory']['offset'],expected['factory']['size'])==(0x10000,0x180000),'Unsupported application slot')
    require(expected['ctx']['size']==0x200000 and expected['clip']['size']==0x70000,'Data partition sizes changed')
    directory=ROOT/'backups'/('kws-'+args.phase+'-'+datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S'))
    directory.mkdir(parents=True,exist_ok=False)
    command=[sys.executable,'-m','esptool','--chip','esp32c3','--port',args.port,'--baud','460800']
    def run(arguments,after='no-reset'):
        with (directory/'esptool.log').open('a',encoding='utf8') as log:
            result=subprocess.run(command+['--after',after]+arguments,stdout=log,stderr=subprocess.STDOUT)
        if result.returncode: raise RuntimeError('esptool failed; see local log '+str(directory))
    full=directory/'flash-before.bin'
    run(['read-flash','0x0','0x400000',str(full)])
    before=full.read_bytes();require(len(before)==0x400000,'Incomplete full backup')
    run(['verify-flash','0x0',str(full)],after='hard-reset' if args.backup_only else 'no-reset')
    require(partitions(before[0x8000:0x9000])==expected,'Device partition table differs; update refused')
    old=before[0x10000:0x190000];old=old[:application_size(old)]
    (directory/'application-before.bin').write_bytes(old)
    report=dict(backup_sha256=sha(before),old_application_sha256=sha(old),new_application_sha256=sha(app),
        new_application_bytes=len(app),application_budget=app_limit(args.protocol_diagnostic,args.kws_cross_diagnostic),
        protocol_diagnostic=args.protocol_diagnostic,kws_cross_diagnostic=args.kws_cross_diagnostic,
        partitions=expected,backup_device_verified=True,installed=False)
    (directory/'manifest.json').write_text(json.dumps(report,indent=2))
    if not args.backup_only:
        run(['write-flash','0x10000',str(build/'esp_hi_agent.bin')])
        run(['verify-flash','0x10000',str(build/'esp_hi_agent.bin')])
        after_file=directory/'flash-after.bin'
        run(['read-flash','0x0','0x400000',str(after_file)],after='hard-reset')
        after=after_file.read_bytes()
        require(before[:0x10000]==after[:0x10000] and before[0x190000:]==after[0x190000:],'Non-application data changed')
        require(after[0x10000:0x10000+len(app)]==app,'Application readback mismatch')
        report.update(installed=True,application_verified=True,data_partitions_unchanged=True,after_sha256=sha(after))
        (directory/'manifest.json').write_text(json.dumps(report,indent=2))
    artifact=ROOT/'artifacts'/('kws-'+args.phase)
    artifact.mkdir(parents=True,exist_ok=True)
    (artifact/'flash-backup-path.txt').write_text(str(directory))
    print(json.dumps(dict(directory=str(directory),**report),indent=2))

if __name__=='__main__': main()
