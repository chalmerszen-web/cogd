"""Rebuild the embedded LVGL Chinese font from the licensed Noto Sans SC source.

Requires fonttools==4.63.0 and Node/npm; lv_font_conv is pinned to 1.5.3.
The source font is supplied explicitly, not downloaded by a firmware build.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

PROJECT = Path(__file__).resolve().parents[1]
RANGES = "0x20-0x7e,0xa0-0xff,0x370-0x3ff,0x2000-0x22ff,0x2460-0x24ff,0x3000-0x303f,0x4e00-0x9fff,0xff00-0xffef"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="NotoSansSC[wght].ttf, under SIL OFL 1.1")
    parser.add_argument("--output", type=Path, default=PROJECT / "components/p4_board/cjk16.bin")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cogd-cjk-") as temp:
        regular = Path(temp) / "NotoSansSC-Regular.ttf"
        font = TTFont(args.source)
        instantiateVariableFont(font, {"wght": 400}, inplace=True)
        font.save(regular)
        subprocess.run([
            "npx.cmd" if os.name == "nt" else "npx", "--yes", "lv_font_conv@1.5.3",
            "--font", str(regular), "--size", "16", "--bpp", "2", "--format", "bin",
            "--range", RANGES, "--no-kerning", "--output", str(args.output),
        ], check=True)
    print(json.dumps({"output": str(args.output), "bytes": args.output.stat().st_size,
                      "sha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
                      "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest()}, indent=2))


if __name__ == "__main__":
    main()
