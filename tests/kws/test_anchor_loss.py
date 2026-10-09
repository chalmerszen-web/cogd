"""Endpoint translation, exclusion and independent objective checks."""
from pathlib import Path
import sys
import unittest
import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from anchor_loss import accepted_anchor_mask, anchor_loss
from event_loss import event_opportunities


class Anchors(unittest.TestCase):
    def test_annotation_and_endpoints(self):
        mask = accepted_anchor_mask([[32256, 32768, 65536], [32768], [32768], [32768]],
            [1, 0, 1, 1], [32768, 0, 0, 33280], [65536] * 4, [256, 256, 127, 256])
        self.assertEqual(mask.shape, (4, 126))
        np.testing.assert_array_equal(np.flatnonzero(mask[0]), [61, 125])
        self.assertFalse(mask[1:].any())

    def test_parent_translation(self):
        original = [32768, 56832]
        translated = [[end - 32768 for end in original], original]
        mask = accepted_anchor_mask(translated, [1, 1], [-1024, 0], [65536, 56832], [256, 222])
        self.assertFalse(mask[0].any())  # Both local endpoints precede warm-up.
        np.testing.assert_array_equal(np.flatnonzero(mask[1]), [61, 108])

    def test_invalid_contract(self):
        for anchors in ([[32768, 32768]], [[32769]], [[32768.0]]):
            with self.assertRaises(ValueError):
                accepted_anchor_mask(anchors, [1], [0], [65536], [256])
        with self.assertRaises(ValueError):
            accepted_anchor_mask([[]], [2], [0], [65536], [256])
        with self.assertRaises(ValueError):
            accepted_anchor_mask([[]], [1], [0], [65536], [257])

    def test_scalar_penalty_and_gradient(self):
        torch.manual_seed(2026100273)
        logits = torch.randn(3, 1, 256, requires_grad=True)
        mask = accepted_anchor_mask([[32768, 56832], [32768], []], [1, 1, 0],
            [0] * 3, [65536] * 3, [256] * 3)
        loss, components = anchor_loss(logits, torch.from_numpy(mask))
        opportunities, _ = event_opportunities(logits)
        values = opportunities.detach().numpy()[mask]
        expected = sum(max(0, 268 / 256 + 1 - float(value)) ** 2 for value in values) / 3
        self.assertAlmostEqual(float(loss.detach()), expected, places=5)
        self.assertEqual(int(components['anchors']), 3)
        loss.backward()
        self.assertTrue(torch.isfinite(logits.grad).all())
        self.assertGreater(float(logits.grad[:2].abs().sum()), 0)
        self.assertEqual(float(logits.grad[2].abs().sum()), 0)

    def test_empty_and_causal_mask(self):
        logits = torch.zeros(1, 1, 256, requires_grad=True)
        empty = torch.zeros(1, 126, dtype=torch.bool)
        loss, _ = anchor_loss(logits, empty)
        self.assertEqual(float(loss.detach()), 0)
        loss.backward()
        self.assertEqual(float(logits.grad.abs().sum()), 0)
        mask = torch.from_numpy(accepted_anchor_mask([[32768]], [1], [0], [65536], [256]))
        original = anchor_loss(logits.detach(), mask)[0]
        changed = logits.detach().clone(); changed[:, :, 128:] = 100
        self.assertEqual(float(original), float(anchor_loss(changed, mask)[0]))


if __name__ == '__main__':
    torch.set_num_threads(1)
    unittest.main()
