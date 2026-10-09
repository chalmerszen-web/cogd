"""Numerical/gradient/mask contracts, synthetic inputs only; no candidate fit."""
from pathlib import Path
import copy
import sys
import numpy as np
import torch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from conditioned_integer_qat import IntegerConditioned,IntegerLayer,integer_context_forward,checkpoint_integer,CONTRACT
from periodic import TOPOLOGY,ENCODING
from quantize import infer,requantize


def verify(model):
    torch.set_num_threads(4)
    torch.manual_seed(20261003428)
    net=IntegerConditioned(model)
    assert sum(p.numel() for p in net.parameters())==9984
    # The Float64 requantizer handles all C shifts and exact half boundaries.
    values=np.array([-16777215,-8388609,-65537,-65536,-32769,-32768,-32767,-257,-255,-129,-128,-127,-3,-2,-1,0,1,2,3,127,128,129,255,257,32767,32768,32769,65536,65537,8388609,16777215],dtype=np.int64)
    for shift in range(-31,32):
        row=dict(model['layers'][-1],weights=[0]*24,bias=[0],shift=[shift])
        layer=IntegerLayer(row,11)
        actual=layer.quantize(torch.tensor(values[None,None,:],dtype=torch.float32)).detach().numpy().astype(np.int16)[0,0]
        expected=requantize(values,shift,-32768,32767)
        np.testing.assert_array_equal(actual,expected)
    # Context masking equals cold C state, never the biases of artificial frames.
    rng=np.random.default_rng(428)
    raw=rng.integers(-128,128,(3,256,90),dtype=np.int8)
    joined=np.zeros((3,90,382),dtype=np.float32)
    joined[:,:,126:]=raw.transpose(0,2,1).astype(np.float32)/32
    active=torch.zeros((3,1,382),dtype=torch.bool);active[:,:,126:]=True
    with torch.no_grad():output,traces=integer_context_forward(net,torch.from_numpy(joined),active,trace=True)
    expected=np.stack([infer(row,model) for row in raw])
    actual=torch.cat(traces,dim=1).transpose(1,2).numpy().astype(np.int16)
    np.testing.assert_array_equal(actual,expected)
    np.testing.assert_array_equal((output[:,0]*256).numpy().astype(np.int16),expected[:,:,-1])
    # Invalid tails are not observable, and changes there cannot affect the past.
    active[1,:,280:]=False
    changed=joined.copy();changed[1,:,280:]=1
    with torch.no_grad():a=integer_context_forward(net,torch.from_numpy(joined),active);b=integer_context_forward(net,torch.from_numpy(changed),active)
    assert torch.equal(a,b) and not a[1,:,154:].any()
    net.zero_grad()
    prediction=integer_context_forward(net,torch.from_numpy(joined),active)
    prediction.square().mean().backward()
    assert all(p.grad is not None and torch.isfinite(p.grad).all() for p in net.parameters())
    assert sum(float(p.grad.abs().sum()) for p in net.parameters())>0
    saved=dict(initial_integer_model=model,state_dict=net.state_dict(),config=dict(
        training_objective=CONTRACT,topology=TOPOLOGY,pitch_encoding=ENCODING,inputs=90,channels=24,post_training_calibrations=0))
    checkpoint_integer(saved).load_state_dict(saved['state_dict'])
    invalid=[]
    for key,value in (('training_objective','old_float_loss'),('post_training_calibrations',1),('inputs',88)):
        case=copy.deepcopy(saved);case['config'][key]=value;invalid.append(case)
    for key in ('layers.0.bias','layers.0.scale'):
        case=copy.deepcopy(saved);case['state_dict'][key]+=1;invalid.append(case)
    for case in invalid:
        try:checkpoint_integer(case)
        except ValueError:pass
        else:raise AssertionError('Invalid exact-integer checkpoint accepted')
    for bad in (torch.zeros(1,88,1),torch.full((1,90,1),.5),torch.full((1,90,1),128.),torch.zeros(1,90,1,dtype=torch.float64)):
        try:net.features(bad)
        except ValueError:pass
        else:raise AssertionError('Invalid integer input accepted')
    return dict(passed=True,trainable_weights=9984,fixed_biases_shifts=True,requantize_shifts=63,
        contextual_integer_values=int(expected.size),masked_prefix_tail_exact=True,
        finite_nonzero_weight_gradients=True,checkpoint_rejections=5,input_rejections=4)
