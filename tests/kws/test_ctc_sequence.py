from pathlib import Path
import sys
import unittest
import numpy as np
import torch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/kws'))
from ctc_sequence import sequence_loss
from ctc_prefix import prefix_loss


def forward(logs,tokens,blank_frames):
    labels=[0]
    for token in tokens:labels.extend((token,0))
    previous=np.full(len(labels),-np.inf);previous[0]=0
    for frame,row in enumerate(logs):
        current=np.full(len(labels),-np.inf)
        for i,token in enumerate(labels):
            if frame<blank_frames and token:continue
            choices=[previous[i]]
            if i:choices.append(previous[i-1])
            if i>1 and token and token!=labels[i-2]:choices.append(previous[i-2])
            current[i]=np.logaddexp.reduce(choices)+row[token]
        previous=current
    return previous[0] if not tokens else np.logaddexp(previous[-1],previous[-2])


class SequenceTest(unittest.TestCase):
    def test_mixed_targets_and_gradients(self):
        torch.set_num_threads(1);torch.manual_seed(454)
        value=torch.randn(4,14,24,dtype=torch.float64,requires_grad=True)
        target=torch.tensor([[1,2,3,4],[5,6,9,9],[9,0,0,0],[0,0,0,0]])
        length=torch.tensor([4,4,1,0])
        result=sequence_loss(value,target,length,4)
        logs=value[:,:,1::2].detach().log_softmax(1).numpy().transpose(0,2,1)
        expected=np.mean([-forward(logs[i],target[i,:int(n)].tolist(),2)/max(int(n),1)
            for i,n in enumerate(length)])
        self.assertAlmostEqual(float(result.detach()),float(expected),places=12)
        result.backward()
        self.assertTrue(bool(torch.isfinite(value.grad).all()))
        self.assertEqual(float(value.grad[:,:,::2].abs().sum()),0)
        self.assertGreater(float(value.grad[:,1:,1:4:2].min()),0)

    def test_complete_word_compatibility_and_guards(self):
        value=torch.randn(2,14,24,dtype=torch.float64)
        target=torch.tensor([[1,2,3,4],[5,6,7,8]])
        torch.testing.assert_close(sequence_loss(value,target,torch.tensor([4,4]),4),
            prefix_loss(value,target,4),rtol=1e-12,atol=1e-12)
        for length in (torch.tensor([-1,4]),torch.tensor([5,4]),torch.tensor([1,4])):
            with self.assertRaises(ValueError):sequence_loss(value,target,length,4)
        with self.assertRaises(ValueError):sequence_loss(value,target,torch.tensor([4,4]),3)


if __name__=='__main__':unittest.main()
