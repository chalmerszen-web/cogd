"""Rebuild the embedded LVGL Chinese font from the licensed Noto Sans SC source.

Requires fonttools==4.63.0 and Node/npm; lv_font_conv is pinned to 1.5.3.
The source font is supplied explicitly, not downloaded by a firmware build.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

PROJECT = Path(__file__).resolve().parents[1]
RANGES = "0x20-0x7e,0xa0-0xff,0x370-0x3ff,0x2000-0x22ff,0x2460-0x24ff,0x3000-0x303f,0x4e00-0x9fff,0xff00-0xffef"
MATH_FONT = Path(__file__).resolve().parent / "fonts/DejaVuSans.ttf"
MATH_SHA256 = "3fdf69cabf06049ea70a00b5919340e2ce1e6d02b0cc3c4b44fb6801bd1e0d22"
NOTO_SHA256 = "a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da"
# A range request does not create glyphs absent from the source font. Keep an
# explicit teaching contract, including one-sided limits and grouped scripts.
REQUIRED_MATH = "⁰¹²³⁴⁵⁶⁷⁸⁹⁺⁻⁼⁽⁾ⁱⁿ₀₁₂₃₄₅₆₇₈₉₊₋₌₍₎αβγδθλμπσφω′″←→↔⇒⇔±×÷−∞√∑∏∫∂∇≈≠≤≥∈∉∅"
REQUIRED = set(range(0x20, 0x7f)) | set(map(ord, REQUIRED_MATH + "题目思路步骤答案检验"))


def binary_codepoints(data):
    """Read the same four cmap formats as LVGL's binfont loader; no rendering."""
    pos = 0
    while pos + 8 <= len(data):
        length, label = struct.unpack_from("<I4s", data, pos)
        if length < 8 or pos + length > len(data):
            raise ValueError("truncated LVGL font table")
        if label == b"cmap":
            if length < 12:
                raise ValueError("truncated LVGL cmap header")
            table = data[pos:pos + length]
            count, = struct.unpack_from("<I", table, 8)
            if 12 + count * 16 > length:
                raise ValueError("truncated LVGL cmap directory")
            covered = set()
            for i in range(count):
                off, start, size, first, entries, kind, _ = struct.unpack_from("<IIHHHBB", table, 12 + 16 * i)
                if not first or not size:
                    raise ValueError("invalid LVGL cmap")
                if kind == 2:  # FORMAT0_TINY
                    covered.update(range(start, start + size))
                elif kind == 0:  # FORMAT0_FULL; noninitial zero offsets are missing.
                    if entries < size or off + entries > length:
                        raise ValueError("truncated full cmap")
                    covered.update(start + j for j in range(size) if j == 0 or table[off + j])
                elif kind in (1, 3):  # SPARSE_FULL / SPARSE_TINY
                    if off + entries * (4 if kind == 1 else 2) > length:
                        raise ValueError("truncated sparse cmap")
                    values = struct.unpack_from("<" + "H" * entries, table, off)
                    if any(v >= size for v in values):
                        raise ValueError("out of range sparse cmap")
                    covered.update(start + v for v in values)
                else:
                    raise ValueError(f"unsupported LVGL cmap type {kind}")
            return covered
        pos += length
    raise ValueError("missing LVGL cmap")


def require_coverage(covered, required=REQUIRED, label="font"):
    missing = sorted(required - set(covered))
    if missing:
        raise ValueError(label + " missing " + ", ".join(f"U+{c:04X}" for c in missing))


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="NotoSansSC[wght].ttf, under SIL OFL 1.1")
    parser.add_argument("--output", type=Path, default=PROJECT / "components/p4_board/cjk16.bin")
    parser.add_argument("--converter", type=Path, help="Local lv_font_conv.js (version 1.5.3); otherwise use the offline npm cache")
    parser.add_argument("--metadata", type=Path, help="Metadata output; defaults to FONT-METADATA.json beside the binary")
    args = parser.parse_args()
    if sha256(args.source) != NOTO_SHA256 or sha256(MATH_FONT) != MATH_SHA256:
        raise ValueError("source font SHA256 differs from the reviewed font sources")
    if args.converter:
        package = json.loads((args.converter.parent / "package.json").read_text(encoding="utf-8"))
        if package.get("name") != "lv_font_conv" or package.get("version") != "1.5.3":
            raise ValueError("converter must be lv_font_conv 1.5.3")
        converter = ["node", str(args.converter)]
    else:
        converter = ["npx.cmd" if os.name == "nt" else "npx", "--offline", "--yes", "lv_font_conv@1.5.3"]
    with tempfile.TemporaryDirectory(prefix="cogd-cjk-") as temp:
        regular = Path(temp) / "NotoSansSC-Regular.ttf"
        with TTFont(args.source) as font, TTFont(MATH_FONT) as math:
            source_cmap = set(font.getBestCmap())
            supplement = REQUIRED - source_cmap
            require_coverage(math.getBestCmap(), supplement, "math source")
            require_coverage(source_cmap | supplement, label="source fonts")
            instantiateVariableFont(font, {"wght": 400}, inplace=True)
            font.save(regular)
        staged = Path(temp) / "cjk16.bin"
        subprocess.run(converter + [
            "--font", str(regular), "--size", "16", "--bpp", "2", "--format", "bin",
            "--range", RANGES, "--font", str(MATH_FONT),
            "--range", ",".join(f"0x{c:x}" for c in sorted(supplement)),
            "--no-kerning", "--output", str(staged),
        ], check=True)
        data = staged.read_bytes()
        require_coverage(binary_codepoints(data), label="generated binary")
        if len(data) + 4096 > 0x180000:
            raise ValueError("font exceeds reserved media font region")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(data)
    metadata = {
        "family": "Noto Sans SC with DejaVu Sans math supplement", "weight": 400,
        "size_px": 16, "bpp": 2, "converter": "lv_font_conv@1.5.3", "fonttools": "4.63.0",
        "ranges": RANGES,
        "source_url": "https://raw.githubusercontent.com/google/fonts/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf",
        "license": "FONT-LICENSE.txt", "latin_runtime_font": "LVGL Montserrat 14; fallback is cjk16.bin",
        "source": {"bytes": args.source.stat().st_size, "sha256": sha256(args.source)},
        "math_supplement": {"source": "tools/fonts/DejaVuSans.ttf", "license": "tools/fonts/DejaVuSans-LICENSE.txt",
                            "sha256": MATH_SHA256, "codepoints": [f"U+{c:04X}" for c in sorted(supplement)]},
        "required_math": REQUIRED_MATH,
        "binary": {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()},
        "requires": "CONFIG_LV_FONT_FMT_TXT_LARGE=y (bitmap offsets exceed 20 bits)"
    }
    (args.metadata or args.output.with_name("FONT-METADATA.json")).write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({"output": str(args.output), **metadata["binary"], "added_glyphs": len(supplement)}, indent=2))


if __name__ == "__main__":
    main()
