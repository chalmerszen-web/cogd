"""Fixed experiment permission and closed-form learning-rate invariants."""
from pathlib import Path
import math
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from optimizer_schedule import CONTRACT, cosine_rate, declared_schedule


class ScheduleTests(unittest.TestCase):
    def test_endpoints_monotonic_bounds_and_symmetry(self):
        values = [cosine_rate(.003, .0003, 6000, i) for i in range(1, 6001)]
        self.assertEqual(values[0], .003)
        self.assertEqual(values[-1], .0003)
        self.assertTrue(all(.0003 <= v <= .003 for v in values))
        self.assertTrue(all(a >= b for a, b in zip(values, values[1:])))
        for i in range(0, 6000, 37):
            self.assertAlmostEqual(values[i] + values[-1 - i], .0033, places=15)
        self.assertAlmostEqual(cosine_rate(.003, .0003, 6001, 3001), .00165, places=15)

    def test_invalid_numeric_contract(self):
        for args in ((math.nan, .0003, 6000, 1), (.003, math.inf, 6000, 1),
            (True, .0003, 6000, 1), (.003, 0, 6000, 1), (.003, .01, 6000, 1),
            (.003, .0003, 1, 1), (.003, .0003, 6000, 0), (.003, .0003, 6000, 6001),
            (.003, .0003, 6000, True), (.003, .0003, 6000., 1)):
            with self.assertRaises(ValueError):
                cosine_rate(*args)

    def test_opt_in_and_legacy_defaults(self):
        self.assertIsNone(declared_schedule({}, {}))
        self.assertIsNone(declared_schedule(dict(hidden=32, matrix_q=8, learning_rate=.003), {}))
        cfg = dict(hidden=64, matrix_q=6, steps=6000, seed=2026100479,
            learning_rate=.003, learning_rate_final=.0003, learning_rate_schedule='cosine_v1',
            named_word_regularizer=True, phonetic_regularizer=False)
        plan = dict(optimization_contract=CONTRACT)
        self.assertEqual(declared_schedule(cfg, plan), (.003, .0003, 6000))
        for key, value in (('hidden', 32), ('matrix_q', 8), ('steps', 6001),
            ('seed', 9), ('learning_rate_schedule', 'unknown'), ('phonetic_regularizer', True),
            ('learning_rate_final', .0001), ('named_word_regularizer', False)):
            with self.assertRaises(ValueError):
                declared_schedule(dict(cfg, **{key: value}), plan)
        with self.assertRaises(ValueError):
            declared_schedule(cfg, {})


if __name__ == '__main__':
    unittest.main(verbosity=2)
