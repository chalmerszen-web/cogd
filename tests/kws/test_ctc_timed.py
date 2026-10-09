from pathlib import Path
import itertools
import sys
import unittest
import numpy as np
import torch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'training/kws'))
from ctc_model import collapse
from ctc_sequence import sequence_loss
from ctc_timed import timed_sequence_loss, audited_final_windows


def probability(logits, wanted, window):
    """Independent full14-state probability recursion, without renormalizing."""
    raw = logits[0, :, 1::2].detach().numpy().T
    raw = raw - raw.max(1, keepdims=True)
    p = np.exp(raw); p /= p.sum(1, keepdims=True)
    states = [0]
    for token in wanted: states.extend((token, 0))
    alpha = np.zeros(len(states), np.float64); alpha[0] = 1
    for t, row in enumerate(p):
        new = np.zeros_like(alpha)
        for s, token in enumerate(states):
            if token == wanted[-1] and not window[0] <= t <= window[1]:
                continue
            total = alpha[s]
            if s: total += alpha[s-1]
            if s >= 2 and token and token != states[s-2]: total += alpha[s-2]
            new[s] = total * row[token]
        alpha = new
    return -np.log(alpha[-2:].sum()) / len(wanted)


class TimedTest(unittest.TestCase):
    def test_probability_and_complete_gradient(self):
        torch.manual_seed(456)
        logits = torch.randn(1, 14, 16, dtype=torch.float64, requires_grad=True)
        targets = torch.tensor([[1, 2, 3, 4]], dtype=torch.int64)
        lengths = torch.tensor([4], dtype=torch.int64)
        window = torch.tensor([[4, 6]], dtype=torch.int64)
        actual = timed_sequence_loss(logits, targets, lengths, window, 0)
        expected = probability(logits, [1, 2, 3, 4], (4, 6))
        self.assertAlmostEqual(float(actual.detach()), expected, places=12)
        actual.backward()
        self.assertTrue(torch.isfinite(logits.grad).all())
        # Full numeric gradient, including the forbidden final-token logits.
        for channel, frame in itertools.product(range(14), range(16)):
            changed = logits.detach().clone()
            changed[0, channel, frame] += 1e-5
            upper = probability(changed, [1, 2, 3, 4], (4, 6))
            changed[0, channel, frame] -= 2e-5
            lower = probability(changed, [1, 2, 3, 4], (4, 6))
            self.assertAlmostEqual(float(logits.grad[0, channel, frame]),
                                   (upper-lower)/2e-5, places=8)
        self.assertTrue((logits.grad[0, 4, [1, 3, 5, 7, 15]] > 0).all())

    def test_original_contract_and_bounds(self):
        torch.manual_seed(457)
        logits = torch.randn(3, 14, 20, dtype=torch.float64, requires_grad=True)
        targets = torch.tensor([[1, 2, 3, 4], [9, 0, 0, 0], [0, 0, 0, 0]])
        lengths = torch.tensor([4, 1, 0])
        none = torch.full((3, 2), -1, dtype=torch.int64)
        a = sequence_loss(logits, targets, lengths, 2)
        b = timed_sequence_loss(logits, targets, lengths, none, 2)
        torch.testing.assert_close(a, b, rtol=0, atol=1e-14)
        ga = torch.autograd.grad(a, logits, retain_graph=True)[0]
        gb = torch.autograd.grad(b, logits, retain_graph=True)[0]
        torch.testing.assert_close(ga, gb, rtol=1e-12, atol=1e-14)
        full = none.clone(); full[0] = torch.tensor([0, 8])
        torch.testing.assert_close(timed_sequence_loss(logits, targets, lengths, full, 2),
                                   a, rtol=0, atol=1e-14)
        for row in ((-1, 3), (5, 4), (0, 9), (0, 2)):
            invalid = none.clone(); invalid[0] = torch.tensor(row)
            with self.assertRaises(ValueError):
                timed_sequence_loss(logits, targets, lengths, invalid, 2)
        invalid = none.clone(); invalid[1] = torch.tensor([4, 6])
        with self.assertRaises(ValueError):
            timed_sequence_loss(logits, targets, lengths, invalid, 2)
        with self.assertRaises(ValueError):
            timed_sequence_loss(logits*10000, targets, lengths, none, 2)

    def test_audited_sample_coordinates(self):
        start = np.array([48992, 61440, 0], dtype=np.int64)
        end = np.array([49312, 62400, 0], dtype=np.int64)
        positive = np.array([True, True, False])
        windows = audited_final_windows(start, end, positive)
        np.testing.assert_array_equal(windows, [[95,100],[119,125],[-1,-1]])
        for i in (0,1):
            self.assertGreaterEqual((windows[i,0]+1)*512,start[i])
            self.assertLessEqual((windows[i,1]+1)*512,end[i]+2560)
            self.assertLess((windows[i,0])*512,start[i])
        for a,b in ((np.array([-1],np.int64),np.array([1024],np.int64)),
                    (np.array([100],np.int64),np.array([99],np.int64)),
                    (np.array([65000],np.int64),np.array([65537],np.int64))):
            with self.assertRaises(ValueError):
                audited_final_windows(a,b,np.array([True]))


if __name__ == '__main__': unittest.main()
