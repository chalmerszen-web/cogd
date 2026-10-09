"""Contract tests for the training-side model and quantization helpers."""
from pathlib import Path
import sys
import numpy as np
import torch
from torch.nn import functional as F

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/kws'))
from model import Model, verify_streaming
from quantize import fold_batch_norm, requantize

def test_streaming_and_parameter_count():
    result=verify_streaming()
    assert result['parameters']==6673 and result['max_abs_error']<1e-7

def test_batch_norm_fold_with_nontrivial_running_statistics():
    rng=np.random.default_rng(91)
    weight=rng.normal(size=(24,40,3)).astype(np.float32)
    bias=rng.normal(size=24).astype(np.float32)
    gamma=rng.normal(size=24).astype(np.float32)
    beta=rng.normal(size=24).astype(np.float32)
    mean=rng.normal(size=24).astype(np.float32)
    variance=rng.uniform(.2,2,size=24).astype(np.float32)
    folded_w,folded_b=fold_batch_norm(weight,bias,gamma,beta,mean,variance,1e-5)
    x=torch.from_numpy(rng.normal(size=(2,40,128)).astype(np.float32))
    raw=F.conv1d(x,torch.from_numpy(weight),torch.from_numpy(bias))
    expected=F.batch_norm(raw,torch.from_numpy(mean),torch.from_numpy(variance),torch.from_numpy(gamma),torch.from_numpy(beta),training=False,eps=1e-5)
    actual=F.conv1d(x,torch.from_numpy(folded_w),torch.from_numpy(folded_b))
    torch.testing.assert_close(actual,expected,atol=6e-5,rtol=2e-5)

def test_rounding_and_saturation():
    np.testing.assert_array_equal(requantize([-257,-255,-3,-1,0,1,3,255,257],1),[-128,-128,-2,-1,0,1,2,127,127])
    np.testing.assert_array_equal(requantize([-65,-2,0,2,64],-1),[-128,-4,0,4,127])

def test_no_future_input_leakage():
    torch.manual_seed(31);model=Model().eval()
    a=torch.randn(1,40,180);b=a.clone();b[:,:,91:]+=100
    with torch.no_grad():left=model(a);right=model(b)
    torch.testing.assert_close(left[:,:,:91],right[:,:,:91],rtol=0,atol=0)

def test_gradient_path_is_finite():
    torch.manual_seed(41);model=Model().train()
    x=torch.randn(2,40,160);labels=torch.zeros(2,1,160);labels[:,:,130:]=1
    loss=F.binary_cross_entropy_with_logits(model(x),labels)
    loss.backward()
    assert torch.isfinite(loss)
    for parameter in model.parameters():
        assert parameter.grad is not None and torch.isfinite(parameter.grad).all()
