"""Offline coverage regression against the actual bundled LVGL binary."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "ESP32-P4-WIFI6-Touch-LCD-3.5"
spec = importlib.util.spec_from_file_location("p4_font_builder", PROJECT / "tools/build_cjk_font.py")
font = importlib.util.module_from_spec(spec)
spec.loader.exec_module(font)


class FontCoverage(unittest.TestCase):
    def test_actual_binary_and_metadata(self):
        data = (PROJECT / "components/p4_board/cjk16.bin").read_bytes()
        meta = json.loads((PROJECT / "components/p4_board/FONT-METADATA.json").read_text(encoding="utf-8"))
        font.require_coverage(font.binary_codepoints(data))
        self.assertEqual(hashlib.sha256(data).hexdigest(), meta["binary"]["sha256"])
        self.assertEqual(len(data), meta["binary"]["bytes"])
        self.assertLessEqual(len(data) + 4096, 0x180000)
        self.assertEqual(font.sha256(font.MATH_FONT), meta["math_supplement"]["sha256"])

    def test_source_and_required_one_sided_limits(self):
        with font.TTFont(font.MATH_FONT) as source:
            font.require_coverage(source.getBestCmap(), set(map(ord,"⁺⁻⁰⁵⁶⁷⁸⁹₀₁₂₃₄₅₆₇₈₉₊₋₌₍₎")))
        self.assertIn(ord("⁺"), font.REQUIRED)
        with self.assertRaisesRegex(ValueError,"U\\+207A"):
            font.require_coverage(font.REQUIRED - {ord("⁺")})

    def test_parser_rejects_corrupt_or_missing_tables(self):
        for data in (b"", struct.pack("<I4s",100,b"cmap"), struct.pack("<I4sI",12,b"cmap",9)):
            with self.assertRaises(ValueError):
                font.binary_codepoints(data)

    def test_cmap_holes_are_not_coverage(self):
        full = struct.pack("<I4sI",31,b"cmap",1) + struct.pack("<IIHHHBB",28,32,3,1,3,0,0) + b"\0\0\2"
        self.assertEqual(font.binary_codepoints(full),{32,34})
        sparse = struct.pack("<I4sI",32,b"cmap",1) + struct.pack("<IIHHHBB",28,0x2070,15,1,2,3,0) + struct.pack("<HH",0,10)
        self.assertEqual(font.binary_codepoints(sparse),{0x2070,0x207a})


if __name__ == "__main__":
    unittest.main()
