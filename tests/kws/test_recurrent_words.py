"""Source-label guards, independent path sums/gradients and runtime projection."""
from pathlib import Path
import itertools
import sys
import unittest
import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from recurrent import Recurrent32, quantize
from recurrent_words import (RecurrentWordJoint32, WORD_CLASSES, source_targets,
                             named_loss, joint_loss, event_projection)
from word_event import event_loss


class NamedWordContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        if torch.__version__ != '2.6.0+cpu':
            raise RuntimeError('Frozen local Torch2.6 CPU required')

    def test_source_classes_are_not_phone_annotations(self):
        ids = np.arange(-1, 8, dtype=np.int64)
        wanted, lengths = source_targets(ids, ids == 0)
        np.testing.assert_array_equal(wanted[:, 0], np.arange(9))
        np.testing.assert_array_equal(lengths, [0] + [1] * 8)
        stored, stored_lengths = source_targets(ids.astype(np.int8), ids == 0)
        np.testing.assert_array_equal(stored, wanted)
        np.testing.assert_array_equal(stored_lengths, lengths)
        self.assertEqual(stored.dtype, np.int64)
        with self.assertRaises(ValueError):
            source_targets(ids.astype(np.uint8), ids == 0)
        with self.assertRaises(ValueError):
            source_targets(ids, ids > 0)
        with self.assertRaises(ValueError):
            source_targets(np.array([8], np.int64), np.array([False]))
        with self.assertRaises(ValueError):
            source_targets(ids.astype(np.float32), ids == 0)

    def test_exact_path_probability_and_every_gradient(self):
        torch.manual_seed(494)
        scores = torch.randn(3, 9, 8, dtype=torch.float64, requires_grad=True)
        targets = torch.tensor([[1], [8], [0]])
        lengths = torch.tensor([1, 1, 0])
        windows = torch.tensor([[1, 2], [-1, -1], [-1, -1]])
        actual = named_loss(scores, targets, lengths, windows, 2)
        probabilities = scores[:, :, 1::2].softmax(1)
        losses = []
        for row, word in enumerate((1, 8, 0)):
            mass = torch.zeros((), dtype=torch.float64)
            for path in itertools.product(range(9), repeat=3):
                collapsed = []
                previous = 0
                for token in path:
                    if token and token != previous:
                        collapsed.append(token)
                    previous = token
                if collapsed != ([word] if word else []):
                    continue
                if row == 0 and path[0] == 1:
                    continue
                term = probabilities[row, 0, 0]
                for frame, token in enumerate(path, 1):
                    term = term * probabilities[row, token, frame]
                mass = mass + term
            losses.append(-mass.log())
        expected = torch.stack(losses).mean()
        torch.testing.assert_close(actual, expected, rtol=1e-12, atol=1e-12)
        a = torch.autograd.grad(actual, scores, retain_graph=True)[0]
        b = torch.autograd.grad(expected, scores)[0]
        torch.testing.assert_close(a, b, rtol=1e-11, atol=1e-12)
        self.assertTrue(bool(torch.isfinite(a).all()))
        with self.assertRaises(ValueError):
            named_loss(scores.detach(), targets, lengths, torch.tensor([[-1,-1],[-1,-1],[-1,-1]]), 2)

    def test_joint_gradients_and_unchanged_runtime_projection(self):
        torch.manual_seed(494)
        logits = torch.randn(2, 11, 40, dtype=torch.float64, requires_grad=True)
        words, lengths = torch.tensor([[1], [7]]), torch.tensor([1, 1])
        events, event_lengths = torch.tensor([[1], [0]]), torch.tensor([1, 0])
        windows = torch.tensor([[5, 12], [-1, -1]])
        combined = joint_loss(logits, words, lengths, events, event_lengths, windows, 4)
        independent = (named_loss(logits[:, :9], words, lengths, windows, 4) +
                       event_loss(logits[:, 9:], events, event_lengths, windows, 4)) / 2
        torch.testing.assert_close(combined, independent, rtol=0, atol=0)
        torch.testing.assert_close(torch.autograd.grad(combined, logits, retain_graph=True)[0],
                                  torch.autograd.grad(independent, logits)[0], rtol=0, atol=0)
        source = RecurrentWordJoint32().double().eval()
        self.assertEqual(sum(p.numel() for p in source.parameters()), 7467)
        projected = event_projection(source)
        self.assertEqual(sum(p.numel() for p in projected.parameters()), 7170)
        values = torch.randn(2, 40, 160, dtype=torch.float64)
        with torch.no_grad():
            full, state = source(values)
            actual, final = projected(values)
            torch.testing.assert_close(actual, full[:, 9:], rtol=0, atol=1e-12)
            torch.testing.assert_close(final, state, rtol=0, atol=0)
            parts, streaming = [], None
            for chunk in values.split([1, 31, 11, 117], dim=-1):
                part, streaming = projected(chunk, streaming)
                parts.append(part)
            torch.testing.assert_close(torch.cat(parts, -1), actual, rtol=0, atol=1e-12)
        bundle = quantize(projected)
        self.assertEqual(sum(value.nbytes for value in bundle.values()), 7364)
        for name, value in source.recurrent.state_dict().items():
            torch.testing.assert_close(value, projected.recurrent.state_dict()[name], rtol=0, atol=0)
        with self.assertRaises(ValueError):
            quantize(source)
        source.train()
        with self.assertRaises(ValueError):
            event_projection(source)
        with self.assertRaises(ValueError):
            event_projection(Recurrent32().eval())


if __name__ == '__main__':
    unittest.main(verbosity=2)
