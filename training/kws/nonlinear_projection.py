"""Host-only QAT of one existing pointwise ReLU representation layer.

The output head, causal backbone, frontend, shifts and C11 topology are fixed.
No new runtime operators or inference buffers are introduced.
"""
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F


def rounded(value):
    return value.sign() * (value.abs() + .5).floor()


def ste(value, exact):
    return value + (exact - value).detach()


class Projection(nn.Module):
    def __init__(self, bundle):
        super().__init__()
        layer, head = bundle['layers'][10:12]
        if (layer['inputs'],layer['outputs'],layer['kernel'],layer['depthwise'],layer['relu']) != (48,48,1,False,True):
            raise ValueError('Expected the frozen48 pointwise ReLU contract')
        if head['bias'] != [-109] or head['shift'] != [2]:
            raise ValueError('Unexpected frozen verifier head')
        w=torch.tensor(layer['weights'],dtype=torch.float32).reshape(48,48)
        b=torch.tensor(layer['bias'],dtype=torch.float32)
        s=torch.pow(2.,torch.tensor(layer['shift'],dtype=torch.float32))
        if 48*16384 + float((b.abs()+16*s).max()) >= 2**24:
            raise ValueError('Exact FP32 accumulator budget exceeded')
        self.weight=nn.Parameter(w.clone())
        self.bias_units=nn.Parameter(b/s)
        self.register_buffer('old_weight',w)
        self.register_buffer('old_bias_units',b/s)
        self.register_buffer('scale',s)
        self.register_buffer('head',torch.tensor(head['weights'],dtype=torch.float32))

    def forward(self, x):
        w=self.weight.clamp(-128,127)
        w=ste(w,rounded(w))
        b=(self.bias_units*self.scale)
        b=ste(b,rounded(b))
        value=F.linear(x,w,b)/self.scale
        h=ste(value,rounded(value)).clamp(0,127)
        value=((h*self.head).sum(-1)-109)/4
        return ste(value,rounded(value)).clamp(-32768,32767)

    @torch.no_grad()
    def constrain(self):
        self.weight.clamp_(-128,127)
        self.bias_units.copy_(torch.maximum(torch.minimum(self.bias_units,self.old_bias_units+16),self.old_bias_units-16))

    def regularization(self):
        return ((self.weight-self.old_weight).square().mean() +
                (self.bias_units-self.old_bias_units).square().mean())*1e-4

    @torch.no_grad()
    def export(self):
        return (rounded(self.weight).to(torch.int8).numpy(),
                rounded(self.bias_units*self.scale).to(torch.int32).numpy())


def integer_projection(inputs, weight, bias, shift, head):
    """Independent INT64 pointwise+head calculation, including both clips."""
    x=np.asarray(inputs,dtype=np.int64)
    accumulator=x@np.asarray(weight,dtype=np.int64).T+np.asarray(bias,dtype=np.int64)
    divisor=np.left_shift(np.ones(48,dtype=np.int64),np.asarray(shift,dtype=np.int64))
    h=np.clip(np.sign(accumulator)*((np.abs(accumulator)+divisor//2)//divisor),0,127)
    a=h@np.asarray(head,dtype=np.int64)-109
    return np.clip(np.sign(a)*((np.abs(a)+2)//4),-32768,32767).astype(np.int16)


class Cycle:
    """Deterministic source visits; no resampling by observed model errors."""
    def __init__(self,indices,rng):
        self.indices=np.asarray(indices,dtype=np.int64).copy()
        if not len(self.indices):raise ValueError('Empty registered training stratum')
        self.indices.sort()
        if len(np.unique(self.indices))!=len(self.indices):raise ValueError('Duplicate stratum index')
        self.rng=rng;self.order=rng.permutation(self.indices);self.at=0
        self.visits=np.zeros(len(self.indices),dtype=np.int64)

    def take(self,n):
        result=[]
        while n:
            amount=min(n,len(self.order)-self.at)
            result.extend(self.order[self.at:self.at+amount])
            self.at+=amount;n-=amount
            if self.at==len(self.order):self.order=self.rng.permutation(self.indices);self.at=0
        result=np.asarray(result,dtype=np.int64)
        np.add.at(self.visits,np.searchsorted(self.indices,result),1)
        return result
