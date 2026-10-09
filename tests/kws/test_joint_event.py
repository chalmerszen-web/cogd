from pathlib import Path
import sys
import unittest
import torch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/kws'))
from joint_event import JointEventModel,joint_loss,event_projection
from word_event import event_loss
from ctc_timed import timed_sequence_loss


class JointEventTest(unittest.TestCase):
    def test_equal_objectives_and_every_logit_gradient(self):
        torch.set_num_threads(1);torch.manual_seed(466)
        values=torch.randn(3,16,20,dtype=torch.float64,requires_grad=True)
        phones=torch.tensor([[1,2,3,4],[9,0,0,0],[0,0,0,0]])
        lengths=torch.tensor([4,1,0]);events=torch.tensor([[1],[0],[0]])
        event_lengths=torch.tensor([1,0,0]);windows=torch.tensor([[5,8],[-1,-1],[-1,-1]])
        actual=joint_loss(values,phones,lengths,events,event_lengths,windows,2)
        a=values.detach()[:,:14].clone().requires_grad_()
        b=values.detach()[:,14:].clone().requires_grad_()
        expected_phone=timed_sequence_loss(a,phones,lengths,windows,2)
        expected_event=event_loss(b,events,event_lengths,windows,2)
        self.assertAlmostEqual(float(actual.detach()),float((expected_phone+expected_event).detach()/2),places=12)
        actual.backward();expected_phone.backward();expected_event.backward()
        self.assertTrue(torch.isfinite(values.grad).all())
        torch.testing.assert_close(values.grad[:,:14],a.grad/2,rtol=0,atol=0)
        torch.testing.assert_close(values.grad[:,14:],b.grad/2,rtol=0,atol=0)
        self.assertEqual(values.grad.numel(),960)

    def test_event_projection_batch_and_stream(self):
        torch.set_num_threads(1);torch.manual_seed(466)
        network=JointEventModel().eval();event=event_projection(network)
        self.assertEqual(sum(p.numel() for p in network.parameters()),19840)
        self.assertEqual(sum(p.numel() for p in event.parameters()),19154)
        with torch.no_grad():
            x=torch.randn(2,40,160)
            expected=network(x)[:,14:];actual=event(x)
            torch.testing.assert_close(actual,expected,rtol=1e-6,atol=1e-7)
            a=b=None
            for frame in x.split(1,-1):
                full,a=network.step(frame,a);part,b=event.step(frame,b)
                torch.testing.assert_close(part,full[:,14:],rtol=1e-6,atol=1e-7)
        self.assertEqual(tuple(event.layers[-1].conv.weight.shape),(2,48,1))

    def test_projection_requires_frozen_exact_class(self):
        with self.assertRaises(ValueError):event_projection(JointEventModel())
        with self.assertRaises(ValueError):joint_loss(torch.zeros(1,2,4),None,None,None,None,None)


if __name__=='__main__':unittest.main()
