"""Independent constituent-loss gradients and exact deployed-row projection."""
from pathlib import Path
import sys
import unittest
import numpy as np
import torch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from recurrent import Recurrent32,quantize,FIELDS
from recurrent_joint import RecurrentJoint32,event_projection,PHONE_CLASSES
from joint_event import joint_loss
from ctc_timed import timed_sequence_loss
from word_event import event_loss


class RecurrentJointContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        if torch.__version__!='2.6.0+cpu':raise RuntimeError('Frozen Torch2.6 CPU required')

    def test_loss_and_every_gradient_match_constituents(self):
        torch.manual_seed(2026100478)
        logits=torch.randn(2,16,160,dtype=torch.float64,requires_grad=True)
        tokens=torch.tensor([[1,2,3,4],[9,0,0,0]],dtype=torch.int64)
        token_lengths=torch.tensor([4,1],dtype=torch.int64)
        events=torch.tensor([[1],[0]],dtype=torch.int64)
        event_lengths=torch.tensor([1,0],dtype=torch.int64)
        windows=torch.tensor([[10,20],[-1,-1]],dtype=torch.int64)
        phone_input=logits[:,:14].detach().clone().requires_grad_(True)
        event_input=logits[:,14:].detach().clone().requires_grad_(True)
        phone=timed_sequence_loss(phone_input,tokens,token_lengths,windows,32)
        event=event_loss(event_input,events,event_lengths,windows,32)
        phone_gradient=torch.autograd.grad(phone,phone_input)[0]
        event_gradient=torch.autograd.grad(event,event_input)[0]
        combined=joint_loss(logits,tokens,token_lengths,events,event_lengths,windows,32)
        gradient=torch.autograd.grad(combined,logits)[0]
        torch.testing.assert_close(combined,(phone+event)/2,rtol=0,atol=0)
        torch.testing.assert_close(gradient,torch.cat((phone_gradient,event_gradient),1)/2,rtol=0,atol=0)
        self.assertTrue(bool(torch.isfinite(gradient).all()))
        self.assertEqual(gradient.numel(),5120)

    def test_event_projection_and_streaming(self):
        torch.manual_seed(2026100478)
        joint=RecurrentJoint32().double().eval()
        projected=event_projection(joint)
        self.assertEqual(PHONE_CLASSES,14)
        self.assertEqual(sum(p.numel() for p in joint.parameters()),7632)
        self.assertEqual(sum(p.numel() for p in projected.parameters()),7170)
        values=torch.randn(2,40,160,dtype=torch.float64)
        with torch.no_grad():
            full,state=joint(values);event,event_state=projected(values)
            torch.testing.assert_close(event,full[:,14:],rtol=0,atol=1e-12)
            torch.testing.assert_close(event_state,state,rtol=0,atol=0)
            recurrent=None;parts=[]
            for chunk in values.split([1,11,31,117],dim=-1):
                result,recurrent=projected(chunk,recurrent);parts.append(result)
            torch.testing.assert_close(torch.cat(parts,dim=-1),event,rtol=0,atol=1e-12)
            torch.testing.assert_close(recurrent,event_state,rtol=0,atol=1e-12)
        for name,value in projected.recurrent.state_dict().items():
            torch.testing.assert_close(value,joint.recurrent.state_dict()[name],rtol=0,atol=0)
        torch.testing.assert_close(projected.output.weight,joint.output.weight[14:],rtol=0,atol=0)
        torch.testing.assert_close(projected.output.bias,joint.output.bias[14:],rtol=0,atol=0)

    def test_integer_coefficient_identity_and_export_guard(self):
        torch.manual_seed(478)
        joint=RecurrentJoint32().eval()
        with self.assertRaises(ValueError):quantize(joint)
        projected=event_projection(joint)
        bundle=quantize(projected)
        source=(joint.recurrent.weight_ih_l0,joint.recurrent.weight_hh_l0,
            joint.recurrent.bias_ih_l0,joint.recurrent.bias_hh_l0,
            joint.output.weight[14:],joint.output.bias[14:])
        for (name,shape,dtype),tensor in zip(FIELDS,source):
            raw=tensor.detach().numpy().astype(np.float64).reshape(shape)*256
            expected=(np.sign(raw)*np.floor(np.abs(raw)+.5)).astype(dtype)
            np.testing.assert_array_equal(bundle[name],expected)
        self.assertEqual(sum(value.nbytes for value in bundle.values()),7364)
        joint.train()
        with self.assertRaises(ValueError):event_projection(joint)
        with self.assertRaises(ValueError):event_projection(Recurrent32().eval())


if __name__=='__main__':unittest.main(verbosity=2)
