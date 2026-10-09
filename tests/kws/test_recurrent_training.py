"""Export-range and atomic invalid-value checks for projected optimization."""
from pathlib import Path
import sys
import unittest
import torch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from recurrent import Recurrent32,quantize
from recurrent_q6 import quantize_q6
from train_recurrent import project_parameters_


class TrainingContract(unittest.TestCase):
    def test_explicit_q6_range_and_unchanged_bias_units(self):
        network=Recurrent32().eval()
        with torch.no_grad():
            for parameter in network.parameters():parameter.fill_(.125)
            network.output.weight[0,0]=-3
            network.output.weight[0,1]=3
            network.output.bias[0]=-200
            network.output.bias[1]=200
        self.assertEqual(project_parameters_(network,matrix_q=6),4)
        bundle=quantize_q6(network).values
        self.assertEqual(bundle['output'][0,:2].tolist(),[-128,127])
        self.assertEqual(bundle['output'][1,0],8)
        self.assertEqual(bundle['output_bias'].tolist(),[-32768,32767])
        self.assertEqual(project_parameters_(network,matrix_q=6),0)

    def test_invalid_scaling_rejected_before_write(self):
        network=Recurrent32()
        with torch.no_grad():network.output.weight[0,0]=3
        for invalid in (7,'6',6.0,True):
            with self.assertRaises(ValueError):project_parameters_(network,matrix_q=invalid)
            self.assertEqual(float(network.output.weight[0,0]),3)

    def test_exact_export_range_preserves_interior(self):
        network=Recurrent32().eval()
        with torch.no_grad():
            for parameter in network.parameters():parameter.fill_(.125)
            network.output.weight[0,0]=-1
            network.output.weight[0,1]=1
            network.output.bias[0]=-200
            network.output.bias[1]=200
        self.assertEqual(project_parameters_(network),4)
        bundle=quantize(network)
        self.assertEqual(bundle['output'][0,0],-128)
        self.assertEqual(bundle['output'][0,1],127)
        self.assertEqual(bundle['output'][1,0],32)
        self.assertEqual(bundle['output_bias'].tolist(),[-32768,32767])
        self.assertEqual(project_parameters_(network),0)

    def test_nonfinite_rejected_before_any_projection(self):
        network=Recurrent32()
        with torch.no_grad():
            network.recurrent.weight_ih_l0[0,0]=2
            network.output.bias[1]=float('nan')
        with self.assertRaisesRegex(ValueError,'Nonfinite'):project_parameters_(network)
        self.assertEqual(float(network.recurrent.weight_ih_l0[0,0]),2)
        with self.assertRaises(ValueError):project_parameters_(torch.nn.Linear(40,2))


if __name__=='__main__':unittest.main(verbosity=2)
