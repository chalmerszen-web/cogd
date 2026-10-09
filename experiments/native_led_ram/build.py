"""Build and audit a RAM-only native binary without invoking ESP-IDF."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
SRC = Path(__file__).resolve().parent
OUT = ROOT / 'artifacts/native-led-20261009/build'
BIN = ROOT / '.toolchains/tools/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    elf = OUT / 'native_led.elf'
    image = OUT / 'native_led.bin'
    gcc = BIN / 'riscv32-esp-elf-gcc.exe'
    flags = ['-march=rv32imc_zicsr_zifencei', '-mabi=ilp32', '-Os', '-g',
             '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
             '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
             '-ffunction-sections', '-fdata-sections', '-msmall-data-limit=0',
             '-mno-relax', '-nostdlib', '-nostartfiles', '-Wall', '-Wextra', '-Werror']
    command = [str(gcc), *flags, str(SRC / 'start.S'), str(SRC / 'native_led.c'),
               '-T', str(SRC / 'ram.ld'), '-Wl,--gc-sections,--no-relax,--build-id=none',
               '-Wl,-Map=' + str(OUT / 'native_led.map'), '-o', str(elf)]
    with (OUT / 'build.log').open('w', encoding='utf-8') as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
        subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32c3',
                        'elf2image', '--output', str(image), str(elf)],
                       check=True, stdout=log, stderr=subprocess.STDOUT)
    for tool, args, dest in [
        ('objdump', ['-d', '-S', str(elf)], 'native_led.disasm'),
        ('size', ['-A', str(elf)], 'size.txt'),
        ('nm', ['-n', str(elf)], 'symbols.txt'),
        ('objcopy', ['-O', 'binary', '--only-section=.text', str(elf),
                     str(OUT / 'instructions.bin')], None),
    ]:
        result = subprocess.run([str(BIN / f'riscv32-esp-elf-{tool}.exe'), *args],
                                check=True, capture_output=True)
        if dest: (OUT / dest).write_bytes(result.stdout)
    info = subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32c3',
                           'image-info', str(image)], check=True, capture_output=True)
    (OUT / 'image-info.txt').write_bytes(info.stdout)
    sections = []
    with elf.open('rb') as f:
        parsed = ELFFile(f)
        assert parsed.header['e_entry'] == 0x40380000
        symbols = parsed.get_section_by_name('.symtab')
        assert symbols.get_symbol_by_name('trap_entry')[0]['st_value'] % 256 == 0
        undefined = [s.name for s in symbols.iter_symbols()
                     if s.name and s['st_shndx'] == 'SHN_UNDEF']
        assert not undefined, undefined
        assert b'\x73\x10\x40\x30' not in parsed.get_section_by_name('.text').data(), 'C3 has no mie CSR'
        for section in parsed.iter_sections():
            if not section['sh_flags'] & 2 or not section['sh_size']: continue
            addr, size = section['sh_addr'], section['sh_size']
            assert (0x40380000 <= addr and addr + size <= 0x40384000) or (
                    0x3fc84000 <= addr and addr + size <= 0x3fc86000), section.name
            sections.append({'name': section.name, 'address': hex(addr), 'bytes': size})
    report = {
        'target': 'ESP32-C3 RV32IMC', 'entry': '0x40380000',
        'link_command': command, 'sections': sections,
        'stack_reserved_bytes': 8192, 'heap_bytes': 0,
        'runtime_libraries': [], 'flash_segments': [],
        'rom_helpers': {'disable_default_watchdog': '0x400000a4'},
        'image_bytes': image.stat().st_size,
        'image_sha256': hashlib.sha256(image.read_bytes()).hexdigest(),
        'code_bytes': (OUT / 'instructions.bin').stat().st_size,
        'sources': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in [SRC / 'start.S', SRC / 'native_led.c', SRC / 'ram.ld']},
    }
    (OUT / 'build-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'link_command'}, indent=2))


if __name__ == '__main__':
    main()
