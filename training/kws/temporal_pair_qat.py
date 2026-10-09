"""Host QAT contract for causal DW/PW interactions; original E/L stay frozen."""
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
from pair_qat import PairedQAT, MIXED, straight_integer, round_away

TEMPORAL=(1,3,5,7,9)
POINTWISE_BYTES=5760
TEMPORAL_BYTES=1200

class TemporalPairedQAT(PairedQAT):
    def __init__(self,models):
        super().__init__(models)
        self.temporal=nn.Parameter(torch.zeros(5,2,24,5))
        for branch in self.branches:
            for i in TEMPORAL:
                bound=branch[i].maximum_partial_sum+5*16384
                if bound>=2**24:raise ValueError('Temporal float32 integer bound exceeded')
                branch[i].maximum_partial_sum=bound

    def forward(self,features,trace=False):
        if features.ndim!=3 or features.shape[1]!=40:
            raise ValueError('Expected batch x40 x256-sample frames')
        pw=straight_integer(self.cross.clamp(-128,127),round_away(self.cross.clamp(-128,127)))
        dw=straight_integer(self.temporal.clamp(-128,127),round_away(self.temporal.clamp(-128,127)))
        inputs=[features,features];traces=[[],[]];ps=ds=0
        for index in range(12):
            outputs=[]
            for branch in (0,1):
                layer=self.branches[branch][index]
                accumulator=layer.accumulate(inputs[branch])
                if index in MIXED:
                    accumulator+=F.conv1d(inputs[1-branch],pw[ps,branch,:,:,None])
                if index in TEMPORAL:
                    accumulator+=F.conv1d(F.pad(inputs[1-branch],(4*layer.dilation,0)),
                        dw[ds,branch,:,None,:],dilation=layer.dilation,groups=24)
                out=layer.quantize(accumulator);outputs.append(out)
                if trace:traces[branch].append(out)
            inputs=outputs
            if index in MIXED:ps+=1
            if index in TEMPORAL:ds+=1
        return torch.stack([torch.cat(v,dim=1) for v in traces],dim=1) if trace else \
            torch.stack([v[:,0,:] for v in inputs],dim=1)

    def weights(self):
        dw=round_away(self.temporal.detach().clamp(-128,127)).to(torch.int8).cpu().numpy()
        return np.concatenate((self.export().reshape(-1),dw.reshape(-1)))

def integer_traces(features,models,weights):
    """Independent F64 integer equations; causal direct tensors, no C rings."""
    weights=np.asarray(weights,dtype=np.int8)
    if weights.shape!=(6960,):raise ValueError('Expected6960 coefficients')
    pw=torch.tensor(weights[:5760].reshape(5,2,24,24),dtype=torch.float64)
    dw=torch.tensor(weights[5760:].reshape(5,2,24,5),dtype=torch.float64)
    inputs=[torch.as_tensor(features,dtype=torch.float64).transpose(1,2)]*2
    traces=[[],[]];ps=ds=0
    for index in range(12):
        outputs=[]
        for b in (0,1):
            l=models[b]['layers'][index]
            oc,ic,k,d=(l[key] for key in ('outputs','inputs','kernel','dilation'))
            original=torch.tensor(l['weights'],dtype=torch.float64)
            original=original.reshape(oc,1,k) if l['depthwise'] else original.reshape(oc,k,ic).transpose(1,2)
            a=F.conv1d(F.pad(inputs[b],(d*(k-1),0)),original,
                torch.tensor(l['bias'],dtype=torch.float64),dilation=d,groups=ic if l['depthwise'] else 1)
            if index in MIXED:a+=F.conv1d(inputs[1-b],pw[ps,b,:,:,None])
            if index in TEMPORAL:a+=F.conv1d(F.pad(inputs[1-b],(4*d,0)),dw[ds,b,:,None,:],dilation=d,groups=24)
            scale=torch.pow(2.,torch.tensor(l['shift'],dtype=torch.float64))[None,:,None]
            v=torch.sign(a)*torch.floor(torch.abs(a)/scale+.5)
            v=v.clamp(0 if l['relu'] else -32768 if index==11 else -128,32767 if index==11 else 127)
            outputs.append(v);traces[b].append(v)
        inputs=outputs
        if index in MIXED:ps+=1
        if index in TEMPORAL:ds+=1
    return torch.stack([torch.cat(v,dim=1) for v in traces],dim=1).to(torch.int16)
