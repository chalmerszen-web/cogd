"""Explicit Q6 optimization, atomic rejection and deployable head projection."""
from pathlib import Path
import sys
import unittest
import torch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from recurrent64 import CONTRACT as Q8_CONTRACT, quantize
from recurrent64_q6 import CONTRACT, quantize_q6
from recurrent_q6 import CONTRACT as OLD32_Q6_CONTRACT
from recurrent_words64 import RecurrentWordJoint64, event_projection
from train_recurrent import project_parameters_


class Q6TrainingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        if torch.__version__ != '2.6.0+cpu':
            raise RuntimeError('Frozen Torch2.6 CPU required')

    def test_permission_rejects_wrong_formats_before_mutation(self):
        network = RecurrentWordJoint64()
        with torch.no_grad():
            network.output.weight[0, 0] = 3
        before = {n: p.clone() for n, p in network.state_dict().items()}
        for invalid in (None, Q8_CONTRACT, OLD32_Q6_CONTRACT, CONTRACT + '_wrong', 6):
            with self.assertRaises(ValueError):
                project_parameters_(network, 6, contract=invalid)
            for n, p in network.state_dict().items():
                torch.testing.assert_close(p, before[n], rtol=0, atol=0)
        with self.assertRaises(ValueError):
            project_parameters_(network, 8, contract=CONTRACT)
        for n, p in network.state_dict().items():
            torch.testing.assert_close(p, before[n], rtol=0, atol=0)
        self.assertEqual(project_parameters_(network, 8), 1)
        self.assertEqual(network.output.weight[0, 0].item(), 127 / 256)

    def test_q6_projection_retains_range_bias_and_removes_auxiliary_head(self):
        network = RecurrentWordJoint64().eval()
        with torch.no_grad():
            for p in network.parameters():
                p.fill_(.125)
            network.recurrent.weight_hh_l0[0, 0] = -3
            network.recurrent.weight_ih_l0[0, 0] = 3
            network.output.weight[9, 0] = 1.25
            network.output.bias[9] = -200
            network.output.bias[10] = 200
        self.assertEqual(project_parameters_(network, 6, contract=CONTRACT), 4)
        self.assertEqual(project_parameters_(network, 6, contract=CONTRACT), 0)
        projected = event_projection(network)
        bundle = quantize_q6(projected)
        self.assertEqual(bundle.contract, CONTRACT)
        self.assertEqual(int(bundle.values['recurrent'][0, 0, 0]), -128)
        self.assertEqual(int(bundle.values['input'][0, 0, 0]), 127)
        self.assertEqual(int(bundle.values['output'][0, 0]), 80)
        self.assertEqual(bundle.values['output_bias'].tolist(), [-32768, 32767])
        self.assertEqual(sum(v.nbytes for v in bundle.values.values()), 20868)
        self.assertEqual(sum(p.numel() for p in projected.parameters()), 20482)
        with self.assertRaises(ValueError):
            quantize(projected)
        with self.assertRaises(ValueError):
            quantize_q6(network)

    def test_nonfinite_is_atomic_with_new_contract(self):
        network = RecurrentWordJoint64()
        for invalid in (float('nan'), float('inf'), float('-inf')):
            with torch.no_grad():
                network.recurrent.weight_ih_l0[0, 0] = 3
                network.output.bias[10] = invalid
            before = network.recurrent.weight_ih_l0.clone()
            with self.assertRaisesRegex(ValueError, 'Nonfinite'):
                project_parameters_(network, 6, contract=CONTRACT)
            torch.testing.assert_close(network.recurrent.weight_ih_l0, before, rtol=0, atol=0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
