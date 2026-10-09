import sys
import unittest
from pathlib import Path
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'training/kws'))
from coverage_sampling import CoverageSampler


class CoverageSamplingTests(unittest.TestCase):
    def sampler(self, **kwargs):
        spec = dict(groups={'positive': np.arange(5), 'negative': np.arange(5, 12)},
            quotas={'positive': 2, 'negative': 3}, steps=5, batch=5, seed=9, count=12)
        spec.update(kwargs)
        return CoverageSampler(**spec)

    def test_every_pass_visits_each_source_once_and_budget_stops(self):
        sampler = self.sampler()
        draws = np.stack([sampler.next() for _ in range(5)])
        positive = draws[:, :2].flatten()
        negative = draws[:, 2:].flatten()
        for start in (0, 5):
            self.assertEqual(set(positive[start:start + 5]), set(range(5)))
        for start in (0, 7):
            self.assertEqual(set(negative[start:start + 7]), set(range(5, 12)))
        report = sampler.report(require_complete=True)
        self.assertEqual(report['positive']['seen'], 5)
        self.assertEqual(report['negative']['seen'], 7)
        self.assertEqual(int(sampler.counts.sum()), 25)
        with self.assertRaises(StopIteration): sampler.next()

    def test_same_seed_has_identical_stream_and_no_replacement_before_pass_end(self):
        a, b = self.sampler(), self.sampler()
        for _ in range(5): np.testing.assert_array_equal(a.next(), b.next())
        np.testing.assert_array_equal(a.counts, b.counts)

    def test_old_failing_quota_cannot_claim_full_coverage(self):
        with self.assertRaisesRegex(ValueError, 'Budget cannot cover'):
            CoverageSampler({'old_negative': np.arange(9364)}, {'old_negative': 4},
                steps=2000, batch=4, seed=1, count=9364)

    def test_missing_actual_negative_or_duplicate_source_is_rejected(self):
        for groups in ({'positive': np.arange(5), 'negative': np.arange(5, 11)},
                       {'positive': np.arange(5), 'negative': np.r_[np.arange(5, 11), 4]}):
            with self.assertRaisesRegex(ValueError, 'partition'):
                self.sampler(groups=groups)

    def test_invalid_quotas_and_incomplete_run_fail(self):
        for quotas in ({'positive': 2}, {'positive': 2, 'negative': 0},
                       {'positive': 2, 'negative': 2}, {'positive': True, 'negative': 4}):
            with self.assertRaises(ValueError): self.sampler(quotas=quotas)
        with self.assertRaises(ValueError): self.sampler().report(require_complete=True)


if __name__ == '__main__': unittest.main()
