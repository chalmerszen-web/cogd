"""Build the font region; the remaining media partition is the device video cache."""
from pathlib import Path
import argparse, struct, zlib
p=argparse.ArgumentParser();p.add_argument('output');a=p.parse_args()
font=(Path(__file__).resolve().parents[1]/'components/p4_board/cjk16.bin').read_bytes()
assert len(font)+4096 <= 0x180000
header=struct.pack('<4sIII',b'CGF1',1,len(font),zlib.crc32(font))
out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True)
out.write_bytes(header+b'\xff'*(4096-len(header))+font)
print(f'{out}: {out.stat().st_size} bytes; font CRC32={zlib.crc32(font):08x}')
