from pathlib import Path
import sys
import unittest
import numpy as np
import torch

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/kws'))
from ctc_prefix import prepend_silence,prefix_loss


def constrained_forward(log_probs,tokens,blank_frames):
    labels=[0]
    for token in tokens:labels.extend((int(token),0))
    previous=np.full(len(labels),-np.inf);previous[0]=0
    for frame,row in enumerate(log_probs):
        current=np.full(len(labels),-np.inf)
        for i,token in enumerate(labels):
            if frame<blank_frames and token!=0:continue
            choices=[previous[i]]
            if i:choices.append(previous[i-1])
            if i>1 and token and token!=labels[i-2]:choices.append(previous[i-2])
            current[i]=np.logaddexp.reduce(choices)+row[token]
        previous=current
    return float(np.logaddexp(previous[-1],previous[-2]))


class PrefixTest(unittest.TestCase):
    def test_exact_constrained_path_and_prefix_gradient(self):
        torch.set_num_threads(1);torch.manual_seed(450)
        scores=torch.randn(2,14,20,dtype=torch.float64,requires_grad=True)
        targets=torch.tensor([[1,2,3,4],[5,6,9,9]])
        objective=prefix_loss(scores,targets,4)
        logs=scores[:,:,1::2].log_softmax(1).detach().numpy().transpose(0,2,1)
        oracle=np.mean([-constrained_forward(logs[i],targets[i],2)/4 for i in range(2)])
        self.assertAlmostEqual(float(objective.detach()),float(oracle),places=12)
        objective.backward()
        expected=scores.detach()[:,:,1:4:2].softmax(1)
        expected[:,0]-=1
        torch.testing.assert_close(scores.grad[:,:,1:4:2],expected/8,rtol=1e-12,atol=1e-12)
        self.assertTrue(bool(torch.isfinite(scores.grad).all()))
        self.assertEqual(float(scores.grad[:,:,::2].abs().sum()),0)

    def test_input_preservation_and_guards(self):
        rng=np.random.default_rng(450)
        data=rng.integers(-128,128,(3,256,40),dtype=np.int8)
        silent=np.tile(rng.integers(-128,128,40,dtype=np.int8),(256,1))
        result=prepend_silence(data,silent)
        np.testing.assert_array_equal(result[:,128:],data)
        for row in result:np.testing.assert_array_equal(row[:128],silent[:128])
        self.assertTrue(result.flags.c_contiguous)
        for frames in (0,1,257):
            with self.assertRaises(ValueError):prepend_silence(data,silent,frames)
        scores=torch.zeros(1,14,16);target=torch.tensor([[1,2,3,4]])
        for frames in (0,1,16):
            with self.assertRaises(ValueError):prefix_loss(scores,target,frames)
        with self.assertRaises(ValueError):prefix_loss(torch.full_like(scores,float('nan')),target,4)


if __name__=='__main__':unittest.main()
