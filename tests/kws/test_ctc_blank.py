from pathlib import Path
import sys
import unittest
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'training/kws'))
from ctc_model import blank_loss


class BlankTest(unittest.TestCase):
    def test_independent_blank_probability_and_gradient(self):
        torch.set_num_threads(1)
        torch.manual_seed(449)
        scores = torch.randn(3, 14, 64, dtype=torch.float64, requires_grad=True)
        objective = blank_loss(scores)
        oracle = -scores[:, :, 1::2].log_softmax(1)[:, 0].mean()
        torch.testing.assert_close(objective, oracle, rtol=1e-12, atol=1e-12)
        actual, = torch.autograd.grad(objective, scores, retain_graph=True)
        expected, = torch.autograd.grad(oracle, scores)
        torch.testing.assert_close(actual, expected, rtol=1e-12, atol=1e-12)
        self.assertTrue(bool((actual[:, 1:, 1::2] > 0).all()))
        self.assertEqual(float(actual[:, :, ::2].abs().sum()), 0)

    def test_boundaries(self):
        for shape in ((0,14,4), (1,13,4), (1,14,0), (1,14,3)):
            with self.assertRaises(ValueError): blank_loss(torch.zeros(shape))
        with self.assertRaises(ValueError): blank_loss(torch.full((1,14,4), float('nan')))
        self.assertAlmostEqual(float(blank_loss(torch.zeros(2,14,8))), 2.6390573, places=6)


if __name__ == '__main__': unittest.main()
