"""Contracts for clean-reference cuts without changing existing train data."""
from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "training/kws"))
from energy_cut import incomplete


class EnergyCutTest(unittest.TestCase):
    @staticmethod
    def word():
        x = np.zeros(36000, dtype=np.int16)
        t = np.arange(14400)
        x[15000:29400] = np.rint(4000*np.sin(2*np.pi*t/61)).astype(np.int16)
        x[320:1600] = 120  # weak synthesis transient, not the spoken word
        return x

    def test_low_energy_leading_transient_cannot_define_midpoint(self):
        source = self.word()
        for fraction in (.4, .5, .6):
            for side in ("prefix", "suffix"):
                part, trace = incomplete(source, 320, 30240, side, fraction)
                self.assertGreater(trace.cut, 15000)
                self.assertLess(trace.cut, 29400)
                self.assertGreater(trace.removed_reference_energy_fraction, .35)
                self.assertGreater(trace.retained_reference_energy_fraction, .35)
                self.assertEqual(part.shape, source.shape)
                self.assertEqual(part.dtype, np.int16)
                np.testing.assert_array_equal(source, self.word())

    def test_gain_padding_and_room_noise_do_not_move_reference_cut(self):
        source = self.word()
        _, original = incomplete(source, 320, 30240, "suffix")
        loud = (source.astype(np.int32)*2).astype(np.int16)
        _, scaled = incomplete(loud, 320, 30240, "suffix")
        self.assertEqual(original.cut, scaled.cut)
        padded = np.pad(source, (768, 128))
        _, shifted = incomplete(padded, 1088, 31008, "suffix")
        self.assertEqual(shifted.cut, original.cut+768)
        room = np.full(len(source), 6000, dtype=np.int16)
        part, noisy = incomplete(room, 320, 30240, "suffix", reference=source,
                                 ambient=np.full(100, 100, dtype=np.int16))
        self.assertEqual(noisy.cut, original.cut)
        self.assertEqual(noisy.reference_pcm_sha256, original.reference_pcm_sha256)
        self.assertEqual(int(part[0]), 100)
        self.assertEqual(int(part[-1]), 6000)

    def test_fade_is_smooth_and_cut_is_repeatable(self):
        source = np.full(16000, 2000, dtype=np.int16)
        a, trace = incomplete(source, 0, len(source), "prefix")
        b, other = incomplete(source, 0, len(source), "prefix")
        np.testing.assert_array_equal(a, b)
        self.assertEqual(trace, other)
        self.assertEqual(int(a[trace.cut-321]), 2000)
        self.assertEqual(int(a[trace.cut]), 0)
        self.assertLessEqual(int(np.abs(np.diff(a.astype(np.int32))).max()), 7)

    def test_invalid_or_impulsive_reference_is_not_silently_admitted(self):
        source = self.word()
        bad = [dict(start=-1), dict(end=len(source)+1), dict(start=True),
               dict(side="both"), dict(fraction=.39), dict(fraction=float("nan")),
               dict(ambient=np.zeros(0, dtype=np.int16)),
               dict(reference=np.zeros(100, dtype=np.int16))]
        for change in bad:
            args = dict(start=320, end=30240, side="prefix")
            args.update(change)
            with self.subTest(change=list(change)), self.assertRaises(ValueError):
                incomplete(source, **args)
        for silent in (np.zeros(16000, dtype=np.int16),
                       np.eye(1, 16000, 8000, dtype=np.int16).ravel()*2000):
            with self.assertRaises(ValueError):
                incomplete(silent, 0, len(silent), "prefix")


if __name__ == "__main__":
    unittest.main()
