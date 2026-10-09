"""Train one existing INT8 branch, retaining its static C11 topology and shifts.

Only the host uses PyTorch. Export produces the existing kws_model_t layout;
no model interpreter, additional branch, state or runtime allocation is added.
"""
from copy import deepcopy
import torch
from torch import nn
from affine_pair_qat import Affine
from pair_qat import round_away

class SingleAffineQAT(nn.Module):
    def __init__(self,metadata):
        super().__init__()
        if len(metadata['layers'])!=12:raise ValueError('Expected fixed 24-channel topology')
        self.metadata=deepcopy(metadata)
        self.layers=nn.ModuleList([Affine(layer,index) for index,layer in enumerate(metadata['layers'])])

    def forward(self,features,trace=False):
        if features.ndim!=3 or features.shape[1]!=40:raise ValueError('Expected batch x40 xframes')
        values=[];x=features
        for layer in self.layers:
            x=layer.quantize(layer.accumulate(x))
            if trace:values.append(x)
        return torch.cat(values,dim=1) if trace else x[:,0,:]

    @torch.no_grad()
    def project(self):
        for layer in self.layers:
            layer.weight.clamp_(-128,127)
            bound=layer.bias_limit/layer.scale.flatten()
            layer.bias_units.copy_(torch.maximum(-bound,torch.minimum(bound,layer.bias_units)))

    def export(self):
        model=deepcopy(self.metadata)
        for layer,row in zip(self.layers,model['layers']):
            weight=round_away(layer.weight.detach().clamp(-128,127)).to(torch.int8)
            if not layer.depthwise:weight=weight.permute(0,2,1)
            row['weights']=weight.contiguous().flatten().cpu().tolist()
            row['bias']=layer.integer_bias().detach().to(torch.int32).cpu().tolist()
            if layer.product_margin+max(abs(v) for v in row['bias'])>=2**23:
                raise ValueError('Export exceeds exact FP32 partial-sum bound')
        return model

def bundle(model,name):
    """Map the verified C metadata fields to the existing static exporter."""
    result=deepcopy(model)
    result['name']=name;result['trained']=True
    result['mean_q8']=result.pop('mean');result['inverse_std_q12']=result.pop('inverse')
    return result
