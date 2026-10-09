from pathlib import Path
import sys
import torch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/kws'))
from train import negative_peaks,masked_bce


def test_sparse_false_trigger_gets_gradient_without_penalizing_positive_clip():
    values=torch.full((3,1,128),-8.)
    values[0,0,80:83]=3.
    values[1,0,80:83]=3.
    values[2,0,80:83]=3.
    output=values.requires_grad_();target=torch.zeros_like(output)
    target[1,0,80:83]=1;target[2,0,80]=-1
    losses,negative=negative_peaks(output,target)
    assert negative.tolist()==[True,False,False]
    assert losses[0]>10*masked_bce(output[:1],target[:1])
    losses.sum().backward()
    assert torch.count_nonzero(output.grad[0])==3
    assert output.grad[0,0,80:83].min()>0
    assert torch.count_nonzero(output.grad[1:])==0
