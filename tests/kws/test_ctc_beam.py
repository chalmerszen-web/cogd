import itertools
import math
from pathlib import Path
import sys
import unittest
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'training/kws'))
from ctc_beam import PrefixBeam


def exhaustive(probabilities):
    result = {}
    for path in itertools.product(range(3), repeat=len(probabilities)):
        collapsed, previous = [], None
        probability = 1.0
        for row, token in zip(probabilities, path):
            probability *= row[token]
            if token and token != previous:
                collapsed.append(token)
            previous = token
        key = tuple(collapsed)
        result[key] = result.get(key, 0.0) + probability
    return result


class BeamTest(unittest.TestCase):
    def test_independent_path_sums(self):
        rng = np.random.default_rng(448)
        for _ in range(20):
            p = rng.uniform(.1, 1, (4, 3))
            p /= p.sum(1, keepdims=True)
            scores = np.full((4, 14), -np.inf)
            scores[:, :3] = np.log(p)
            beam = PrefixBeam(width=256)
            actual = dict(beam.push(scores))
            expected = exhaustive(p)
            self.assertEqual(set(actual), set(expected))
            for prefix, probability in expected.items():
                self.assertAlmostEqual(actual[prefix], math.log(probability), places=12)

    def test_repeat_blank_competitor_and_prefix(self):
        values = np.full((8, 14), -30.0)
        values[0:2, 0] = math.log(.55)
        values[0:2, 1] = math.log(.45)
        for frame, token in enumerate([2, 3, 4, 0, 0, 0], 2):
            values[frame, token] = 0
        self.assertEqual(PrefixBeam().push(values)[0][0], (1, 2, 3, 4))
        # No lexicon can invent NI after it was absent from the input.
        values[0:2, 1] = -30
        self.assertEqual(PrefixBeam().push(values)[0][0], (2, 3, 4))
        values[0:2, 1] = math.log(.45)
        values[4, 4], values[4, 9] = -30, 0
        self.assertEqual(PrefixBeam().push(values)[0][0], (1, 2, 3, 9))
        repeated = np.full((4, 14), -30.0)
        repeated[np.arange(4), [9, 0, 9, 0]] = 0
        self.assertEqual(PrefixBeam().push(repeated)[0][0], (9, 9))

    def test_stream_partition_and_guards(self):
        values = np.random.default_rng(448).normal(size=(29, 14))
        full = PrefixBeam().push(values)
        stream = PrefixBeam()
        for start, end in [(0, 1), (1, 7), (7, 8), (8, 24), (24, 29)]:
            stream.push(values[start:end])
        self.assertEqual(stream.ranked(), full)
        stream.reset()
        self.assertEqual(stream.ranked(), [((), 0.0)])
        self.assertEqual(stream.push(values), full)
        for invalid in (np.zeros(13), np.full(14, np.nan), np.full(14, np.inf),
                        np.full(14, -np.inf)):
            with self.assertRaises(ValueError): stream.step(invalid)
        short = PrefixBeam(max_frames=1)
        short.step(values[0])
        with self.assertRaises(ValueError): short.step(values[1])
        self.assertEqual(short.frames, 1)


if __name__ == '__main__':
    unittest.main()
