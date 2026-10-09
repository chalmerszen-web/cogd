"""Build a deterministic RAM-only USB sender with the existing native startup."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from elftools.elf.elffile import ELFFile

ROOT=Path(__file__).resolve().parents[2]
SRC=Path(__file__).resolve().parent
NATIVE=ROOT/'experiments/native_led_ram'
OUT=ROOT/'artifacts/clock-20261010/request-build'
BIN=ROOT/'.toolchains/tools/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    elf=OUT/'request.elf'; image=OUT/'request.bin'
    command=[str(BIN/'riscv32-esp-elf-gcc.exe'),'-march=rv32imc_zicsr_zifencei',
        '-mabi=ilp32','-Os','-g','-ffreestanding','-fno-builtin','-fno-stack-protector',
        '-fno-unwind-tables','-fno-asynchronous-unwind-tables','-ffunction-sections',
        '-fdata-sections','-msmall-data-limit=0','-mno-relax','-nostdlib','-nostartfiles',
        '-Wall','-Wextra','-Werror',str(NATIVE/'start.S'),str(SRC/'request.c'),
        '-T',str(NATIVE/'ram.ld'),'-Wl,--gc-sections,--no-relax,--build-id=none','-o',str(elf)]
    with (OUT/'build.log').open('w',encoding='utf8') as log:
        subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT)
        subprocess.run([sys.executable,'-m','esptool','--chip','esp32c3','elf2image',
            '--output',str(image),str(elf)],check=True,stdout=log,stderr=subprocess.STDOUT)
    with elf.open('rb') as f:
        parsed=ELFFile(f);assert parsed.header['e_entry']==0x40380000
        symbols=parsed.get_section_by_name('.symtab')
        assert symbols.get_symbol_by_name('trap_entry')[0]['st_value']%256==0
        assert not [s.name for s in symbols.iter_symbols() if s.name and s['st_shndx']=='SHN_UNDEF']
        for section in parsed.iter_sections():
            if section['sh_flags']&2 and section['sh_size']:
                a,n=section['sh_addr'],section['sh_size']
                assert 0x40380000<=a and a+n<=0x40384000 or 0x3fc84000<=a and a+n<=0x3fc86000
    report={'image_bytes':image.stat().st_size,'sha256':hashlib.sha256(image.read_bytes()).hexdigest(),
        'ram_only':True,'entry':'0x40380000','command':command,
        'sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in [SRC/'request.c',NATIVE/'start.S',NATIVE/'ram.ld']}}
    (OUT/'build.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('command','sources')}))

if __name__=='__main__': main()
