"""Actual 64-channel projection and all-head gradient/format contracts."""
from pathlib import Path
import sys
import unittest
import torch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from recurrent64 import Recurrent64, quantize
from recurrent import Recurrent32
from recurrent_words64 import RecurrentWordJoint64, event_projection, WORD_CLASSES, joint_loss
from train_recurrent import project_parameters_


class NamedWord64Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        if torch.__version__ != '2.6.0+cpu':
            raise RuntimeError('Frozen Torch2.6 CPU required')

    def test_projection_and_streaming_exact(self):
        torch.manual_seed(501)
        network = RecurrentWordJoint64().double().eval()
        self.assertEqual(sum(p.numel() for p in network.parameters()), 21067)
        projected = event_projection(network)
        self.assertEqual(sum(p.numel() for p in projected.parameters()), 20482)
        x = torch.randn(2, 40, 160, dtype=torch.float64)
        with torch.no_grad():
            full, expected = network(x)
            actual, state = projected(x)
            torch.testing.assert_close(actual, full[:, WORD_CLASSES:], rtol=0, atol=1e-12)
            torch.testing.assert_close(state, expected, rtol=0, atol=0)
            for name, value in network.recurrent.state_dict().items():
                torch.testing.assert_close(value, projected.recurrent.state_dict()[name], rtol=0, atol=0)
            torch.testing.assert_close(projected.output.weight, network.output.weight[9:], rtol=0, atol=0)
            parts = []; state = None
            for chunk in x.split([1, 31, 11, 117], dim=-1):
                output, state = projected(chunk, state); parts.append(output)
            torch.testing.assert_close(torch.cat(parts, -1), actual, rtol=0, atol=1e-12)
        self.assertEqual(sum(v.nbytes for v in quantize(projected).values()), 20868)
        with self.assertRaises(ValueError): quantize(network)
        with self.assertRaises(ValueError): event_projection(Recurrent64().eval())
        with self.assertRaises(ValueError): event_projection(Recurrent32().eval())
        network.train()
        with self.assertRaises(ValueError): event_projection(network)

    def test_actual_model_all_head_gradients(self):
        torch.manual_seed(501)
        network = RecurrentWordJoint64().double().train()
        logits, _ = network(torch.randn(2, 40, 40, dtype=torch.float64))
        words = torch.tensor([[1], [7]]); sizes = torch.ones(2, dtype=torch.int64)
        events = torch.tensor([[1], [0]]); event_sizes = torch.tensor([1, 0])
        windows = torch.tensor([[5, 12], [-1, -1]])
        objective = joint_loss(logits, words, sizes, events, event_sizes, windows, 4)
        objective.backward()
        self.assertTrue(all(p.grad is not None and torch.isfinite(p.grad).all() for p in network.parameters()))
        self.assertTrue(bool((network.output.weight.grad.abs().sum(1) > 0).all()))

    def test_q8_projection_rejects_q6_before_mutation(self):
        network = RecurrentWordJoint64()
        with torch.no_grad():
            network.recurrent.weight_hh_l0[0, 0] = .75
            network.output.bias[0] = 150
        before = {n: p.clone() for n, p in network.state_dict().items()}
        with self.assertRaises(ValueError): project_parameters_(network, 6)
        for n, p in network.state_dict().items(): torch.testing.assert_close(p, before[n], rtol=0, atol=0)
        self.assertEqual(project_parameters_(network, 8), 2)
        self.assertEqual(network.recurrent.weight_hh_l0[0, 0].item(), 127 / 256)
        self.assertEqual(network.output.bias[0].item(), 32767 / 256)
        with torch.no_grad(): network.output.bias[0] = float('nan')
        frozen = network.recurrent.weight_hh_l0.clone()
        with self.assertRaises(ValueError): project_parameters_(network, 8)
        torch.testing.assert_close(network.recurrent.weight_hh_l0, frozen, rtol=0, atol=0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
