"""Automatic screening cannot silently accept extra words or wrong language."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/"training/tts"))
from screen_compact import reason


class CompactScreenTest(unittest.TestCase):
    def test_exact_language_and_complete_homophone_only(self):
        self.assertIsNone(reason("你好，小言。", "<|zh|>", "zh"))
        self.assertIsNone(reason("你好小賢。", "<|yue|>", "yue"))
        for text, detected, language in (("你好小賢", "<|zh|>", "yue"),
            ("你好小燕", "<|yue|>", "yue"), ("你好小嚴", "<|yue|>", "yue"),
            ("你好小言今日天氣好", "<|yue|>", "yue"),
            ("你好", "<|zh|>", "zh"), ("Hi你好小言", "<|zh|>", "zh")):
            with self.subTest(text=text, language=language):
                self.assertIsNotNone(reason(text, detected, language))

    def test_boundary_failure_is_not_overridden_by_transcript(self):
        self.assertEqual(reason("你好小言", "<|zh|>", "zh", "uncertain_end"),
                         "energy_interval_uncertain")


if __name__ == "__main__":
    unittest.main()
