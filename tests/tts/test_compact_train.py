"""Source isolation contracts for the bounded bilingual compact pilot."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/"training/tts"))
from compact_train import requests


def rows(voices):
    return [dict(source_group="kokoro-v1-"+voice, split="train", language=language, label=1)
            for voice in voices for language in ("zh", "yue")]


class CompactTrainTest(unittest.TestCase):
    def test_all_descendants_are_checked_including_negative_ones(self):
        source = rows(["zf_xiaoni"])
        for split in ("validation", "test"):
            contaminated = source+[dict(source[0], split=split, label=0)]
            with self.subTest(split=split), self.assertRaises(ValueError):
                requests(contaminated, ["zf_xiaoni"])

    def test_former_test_identity_uses_current_membership_without_global_override(self):
        source = rows(["af_heart", "am_michael"])
        generated = requests(source, ["af_heart", "am_michael"])
        self.assertEqual(len(generated), 8)
        self.assertTrue(all(row["split"] == "train" for row in generated))
        source[0]["split"] = "test"
        with self.assertRaises(ValueError):
            requests(source, ["af_heart", "am_michael"])

    def test_full_plan_has_distinct_seeds_and_exact_compact_targets(self):
        voices = ["zf_xiaobei", "zf_xiaoni", "zf_xiaoyi", "zm_yunjian",
                  "zm_yunxi", "zm_yunyang", "af_heart", "am_michael"]
        generated = requests(rows(voices), voices)
        self.assertEqual(len(generated), 32)
        self.assertEqual(len({row["clip_id"] for row in generated}), 32)
        self.assertEqual(len({row["seed"] for row in generated}), 32)
        for row in generated:
            self.assertIn(row["source_text"], ("你好小言", "你好小賢"))
            self.assertEqual(row["reference_speaker_id"] if "reference_speaker_id" in row else row["source_group"],
                             "kokoro-v1-"+row["voice"])
            self.assertLess(row["seed"], 2**32)
            self.assertEqual(row["label"], 1)

    def test_missing_language_duplicate_and_unbounded_inputs_fail(self):
        source = rows(["zf_xiaoni"])
        bad = [dict(voices=[]), dict(voices=["zf_xiaoni", "zf_xiaoni"]),
               dict(voices=["../reference"]), dict(speeds=[1., 1.5]),
               dict(speeds=[1., 1.]), dict(seed=2**32)]
        for change in bad:
            args = dict(rows=source, voices=["zf_xiaoni"])
            args.update(change)
            with self.subTest(change=list(change)), self.assertRaises(ValueError):
                requests(**args)
        with self.assertRaises(ValueError):
            requests(source[:1], ["zf_xiaoni"])


if __name__ == "__main__":
    unittest.main()
