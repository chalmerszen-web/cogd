"""Use the unchanged shared seed exporter with the explicit P4 decision source.

The recorded compiler command and hash list both include the substitution.
Keeping the shared generator unchanged preserves C3's existing seed hashes.
"""
import hashlib,importlib.util,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
owner=ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/board/kws_temporal_owner.c'
spec=importlib.util.spec_from_file_location('shared_seed',ROOT/'tools/kws/generate_verified_silence.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
original_run=module.subprocess.run
wide=False

def compile_p4(command,*args,**kwargs):
    if isinstance(command,list) and command and command[0]=='gcc':
        original=str(module.KWS/'kws_temporal_owner.c')
        assert command.count(original)==1
        command[command.index(original)]=str(owner)
        if wide:
            command[command.index(str(module.KWS/'kws_verified.c'))]=str(ROOT/policy['wide_runtime_source'])
            command[command.index(str(ROOT/'tools/kws/export_verified_silence.c'))]=str(ROOT/exporter)
            command.insert(1,'-I'+str(owner.parent))
        if policy.get('private_kernel_dir'):
            kernel=ROOT/policy['private_kernel_dir']
            for name in ('kws_nn.c','kws_frontend.c'):command[command.index(str(module.KWS/name))]=str(kernel/name)
            command[command.index('-DKWS_BUILD_CHANNELS=48')]='-DKWS_BUILD_CHANNELS=96'
            command[1:1]=['-I'+str(kernel/'include'),'-I'+str(kernel)]
    return original_run(command,*args,**kwargs)

if __name__=='__main__':
    out=Path(sys.argv[sys.argv.index('--out')+1]).resolve()
    policy=json.loads((out/'runtime.json').read_text(encoding='utf8'))
    owner=ROOT/policy['temporal_source']
    assert owner.parent==ROOT/'ESP32-P4-WIFI6-Touch-LCD-3.5/board'
    assert owner.name in ('kws_temporal_owner.c','sync_kws_temporal_owner.c')
    exporter=policy.get('seed_exporter_source','ESP32-P4-WIFI6-Touch-LCD-3.5/tools/export_wide_silence.c')
    wide=bool(policy.get('wide_runtime_source'))
    if wide:
        module.SOURCES.extend([policy['wide_runtime_source'],policy['wide_header_source'],exporter])
        if policy.get('temporal_header_source'):module.SOURCES.append(policy['temporal_header_source'])
        if policy.get('contrast_header_source'):module.SOURCES.append(policy['contrast_header_source'])
        if policy.get('private_kernel_dir'):
            kernel=ROOT/policy['private_kernel_dir']
            for name,digest in policy['private_kernel_hashes'].items():
                path=kernel/name;assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
                module.SOURCES.append(path.relative_to(ROOT).as_posix())
    assert ROOT/policy['temporal_source']==owner
    assert policy['sha256']==hashlib.sha256(owner.read_bytes()).hexdigest()
    assert policy['base_owner_sha256']==hashlib.sha256((module.KWS/'kws_temporal_owner.c').read_bytes()).hexdigest()
    module.SOURCES.extend([owner.relative_to(ROOT).as_posix(),Path(__file__).resolve().relative_to(ROOT).as_posix(),
                           (out/'runtime.json').relative_to(ROOT).as_posix()])
    module.subprocess.run=compile_p4
    module.main()
    if wide:
        path=out/'kws_verified_seed.json';metadata=json.loads(path.read_text())
        topology=policy.get('topology',[48,48,48])
        metadata.update(topology=topology,capacity=max(topology),frontend=policy.get('frontend','fixed_logmel_no_pcen'),exported_state='P4-only typed causal state with topology-specific fields')
        path.write_text(json.dumps(metadata,indent=2),encoding='utf8')
