"""Read linked sizes and measured state; never substitute estimates for samples."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
def read(path): return json.loads(path.read_text(encoding='utf-8-sig'))
def flash_bytes(item):
    return sum(v['size'] for k,v in item['memory_types'].items() if k.startswith('Flash'))+sum(v['size'] for k,v in item['memory_types'].get('DRAM',{}).get('sections',{}).items() if k in ('.iram0.text','.dram0.data'))
def main():
    p=argparse.ArgumentParser();p.add_argument('--out',default='artifacts/kws-phase1');args=p.parse_args();out=ROOT/args.out
    archive=read(out/'size-archives.json');files=read(out/'size-files.json')
    selected={k:dict(total=v['size'],flash=flash_bytes(v)) for k,v in archive.items() if 'libkws_c11.a' in k or 'libagent_speech.a' in k}
    core=sum(v['flash'] for v in selected.values())
    # Net main growth is useful but cannot bound additions when old keyword
    # code disappears. Charge the ENTIRE runtime and USB translation units.
    legacy=read(out/'legacy-size-archives.json')
    main_delta=max(0,flash_bytes(archive['esp-idf/main/libmain.a'])-flash_bytes(legacy['esp-idf/main/libmain.a']))
    # Conservative: add every positive size delta from other linked archives.
    other_deltas={k:max(0,flash_bytes(v)-flash_bytes(legacy[k])) if k in legacy else flash_bytes(v)
        for k,v in archive.items() if k not in selected and k!='esp-idf/main/libmain.a'}
    runtime_charge=sum(flash_bytes(v) for k,v in files.items() if 'runtime.c.obj' in k or 'kws_usb.c.obj' in k)
    conservative=core+runtime_charge+sum(other_deltas.values())
    for filename in ('noaudio-size-archives.json','legacy-size-archives.json'):
        assert not any('libkws_c11.a' in k for k in read(out/filename))
    app=(ROOT/'build-kws-probe/esp_hi_agent.bin').read_bytes()
    report=dict(application_bytes=len(app),application_sha256=hashlib.sha256(app).hexdigest(),slot_bytes=0x180000,
        slot_free=0x180000-len(app),kws_archives=selected,kws_code_tables_weights_bytes=core,
        main_flash_growth_bytes=main_delta,whole_runtime_and_usb_flash_charge=runtime_charge,
        other_positive_flash_deltas={k:v for k,v in other_deltas.items() if v},
        conservative_added_backend_flash_bytes=conservative,
        usb_diagnostic_file=next(v for k,v in files.items() if 'kws_usb.c.obj' in k),
        disabled_backend_has_no_linked_storage=True)
    assert len(app)<=1540096 and conservative<=65536
    report['flash_gates_passed']=True
    if (out/'microphone-benchmark.json').exists():
        bench=read(out/'microphone-benchmark.json')
        report['microphone_complete']=bench['complete']
        if bench['complete']:
            report['workspace']=bench['profile']['workspace']
            report['microphone_profile']=bench['profile']
            report['listening_min_heap']=min(r['status']['min_heap'] for r in bench['samples'][1:])
            report['listening_smallest_largest_block']=min(r['status']['largest_block'] for r in bench['samples'][1:])
            assert report['workspace']<=16384
    (out/'resources.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if not isinstance(v,dict)},indent=2))

if __name__=='__main__':main()
