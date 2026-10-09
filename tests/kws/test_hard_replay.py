"""Caps, deterministic ordering, role separation, and bounded replay."""
from pathlib import Path
import sys
import unittest
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'training/kws'))
from hard_replay import recording_families, source_capped_schedule


class HardReplay(unittest.TestCase):
    def test_known_adapters_share_recording_and_other_words_remain_distinct(self):
        names = recording_families(np.array(['channel-a-prefix', 'a-suffix', 'a',
            'weak-logmel-device-extra-a-prefix', 'device-extra-a', 'b']))
        self.assertEqual(names.tolist(), ['a', 'a', 'a', 'device-extra-a', 'device-extra-a', 'b'])

    def test_source_roles_caps_and_preexisting_over_cap_are_preserved(self):
        families = np.array(['a', 'a', 'a', 'b', 'b', 'c'])
        labels = np.array([0, 0, 1, 0, 0, 0])
        base = np.array([1, 2, 1, 7, 7, 1])
        prior = np.array([1, 0, 1, 0, 0, 0])
        original = (base.copy(), prior.copy())
        result = source_capped_schedule(families, labels, np.array([0, 1, 2, 3, 5]), base, prior,
            steps=40, target=9, seed=42)
        self.assertEqual(result['extra_draws'], 20)
        self.assertEqual(result['positive_draws'], 7)
        self.assertEqual(result['negative_draws'], 13)
        np.testing.assert_array_equal(base, original[0])
        np.testing.assert_array_equal(prior, original[1])
        self.assertNotIn(3, [row['index'] for row in result['schedule']])
        roles = {(row['recording'], row['label']): row for row in result['families']}
        self.assertEqual(roles['a', 0]['final_source_role_draws'], 9)
        self.assertEqual(roles['a', 1]['final_source_role_draws'], 9)
        self.assertEqual(roles['b', 0]['final_source_role_draws'], 14)
        self.assertEqual(roles['b', 0]['extra_draws'], 0)
        self.assertEqual(roles['c', 0]['final_source_role_draws'], 9)
        steps = [row['step'] for row in result['schedule']]
        self.assertEqual(len(set(steps)), len(steps))
        self.assertTrue(all(1 <= step <= 40 for step in steps))
        reordered = source_capped_schedule(families, labels, np.array([5, 3, 2, 1, 0]), base, prior,
            steps=40, target=9, seed=42)
        self.assertEqual(reordered, result)

    def test_added_unselected_variants_do_not_multiply_the_cap(self):
        first = source_capped_schedule(np.array(['a']), np.array([0]), np.array([0]), np.array([2]), np.array([0]),
            steps=20, target=8, seed=4)
        second = source_capped_schedule(np.array(['a', 'a', 'a']), np.zeros(3), np.array([0]),
            np.array([2, 0, 0]), np.zeros(3, dtype=int), steps=20, target=8, seed=4)
        self.assertEqual(first['extra_draws'], second['extra_draws'])
        self.assertEqual(first['schedule'], second['schedule'])

    def test_insufficient_budget_and_invalid_metadata_fail(self):
        base = dict(families=np.array(['a', 'a']), labels=np.array([0, 1]), selected=np.array([0]),
            base_counts=np.ones(2, dtype=int), existing_counts=np.zeros(2, dtype=int), steps=4, target=9, seed=1)
        with self.assertRaises(ValueError):
            source_capped_schedule(**base)
        for key, value in (('selected', np.array([0, 0])), ('labels', np.array([0, 2])),
            ('base_counts', np.array([-1, 2])), ('existing_counts', np.array([0.])),
            ('families', np.array(['', 'a'])), ('selected', np.array([2]))):
            bad = dict(base, steps=40)
            bad[key] = value
            with self.assertRaises(ValueError):
                source_capped_schedule(**bad)
        with self.assertRaises(ValueError):
            recording_families(np.array(['']))


if __name__ == '__main__':
    unittest.main()
