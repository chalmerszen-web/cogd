"""Measure linked KWS constants separately from live heap/workspace readings."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
NM=ROOT/'.toolchains/tools/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin/riscv32-esp-elf-nm.exe'

def measure(path,widths):
    output=subprocess.check_output([str(NM),'-S',str(path)],text=True)
    rows=[]
    for line in output.splitlines():
        parts=line.split()
        if len(parts)!=4:continue
        address,size,kind,name=parts
        if re.fullmatch(r'[wbs]\d+|layers|mean|inverse_std|kws_(?:trained_model|verified_primary|verified_secondary|verified_silence)',name):
            rows.append({'address':address,'bytes':int(size,16),'kind':kind,'symbol':name})
    assert sorted(r['bytes'] for r in rows if r['symbol']=='w0')==sorted(120*w for w in widths)
    assert len([r for r in rows if r['symbol']=='layers'])==3
    groups={}
    for row in rows:
        n=row['symbol']
        group='silence_seed' if n=='kws_verified_silence' else 'weights_biases_shifts' if re.fullmatch(r'[wbs]\d+',n) else 'descriptors_normalization'
        groups[group]=groups.get(group,0)+row['bytes']
    return {'elf_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'groups':groups,
            'linked_constant_bytes':sum(groups.values()),'symbols':rows}

def main():
    p=argparse.ArgumentParser();p.add_argument('--elf',type=Path,required=True)
    p.add_argument('--baseline',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--wide',action='store_true');p.add_argument('--mega',action='store_true');a=p.parse_args()
    before=measure(a.baseline,(24,24,48));after=measure(a.elf,(96,96,48) if a.mega else (48,48,48) if a.wide else (24,24,48))
    report={'baseline':before,'candidate':after,'delta_bytes':after['linked_constant_bytes']-before['linked_constant_bytes'],
        'scope':'Linked model arrays, normalization, descriptors and typed silence seed; excludes shared frontend tables, executable code and pooled model-name strings',
        'storage':'Flash rodata; current P4 CONFIG_SPIRAM_RODATA also copies/maps rodata into PSRAM. Do not add this to internal KWS workspace.',
        'runtime':'Read model_heap from agent wake status and workspace from agent kws profile on the board separately.'}
    a.out.write_text(json.dumps(report,indent=2),encoding='utf8')
    print(json.dumps({'constants':after['groups'],'total':after['linked_constant_bytes'],'delta':report['delta_bytes']}))

if __name__=='__main__':main()
