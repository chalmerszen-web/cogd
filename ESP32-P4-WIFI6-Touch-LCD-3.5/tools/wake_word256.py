"""P4 experimental256-channel,32ms-hop, five-class causal keyword network.

Last temporal block has kernel1. Receptive field is63x32ms=2.016s.
The16ms-shaped output repeats each32ms result for host loss compatibility;
only odd positions are meaningful. It must never be deployed at16ms ticks.
"""
import importlib.util
from pathlib import Path
import torch
from torch import nn
from torch.nn import functional as F
_spec=importlib.util.spec_from_file_location('p4_word_backbone',Path(__file__).parent/'kws96_training/model.py')
_module=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(_module)
Causal=_module.Causal
CLASSES=('background','nihao_zh','hi_zh','nihao_yue','hi_yue')
CONTRACT='p4_word256_hop512_rf32256_v1'

class Model(nn.Module):
    def __init__(self):
        super().__init__();self.channels=256
        layers=[Causal(40,256,3)]
        for d in (1,2,4,8):layers.extend((Causal(256,256,5,d,256),Causal(256,256,1)))
        layers.extend((Causal(256,256,1,groups=256),Causal(256,256,1),Causal(256,5,1,bias=True)))
        self.layers=nn.ModuleList(layers);self.norms=nn.ModuleDict({str(i):nn.BatchNorm1d(256) for i in (0,2,4,6,8,10)})
    def forward(self,x):
        assert x.shape[-1]%2==0
        x=x[:,:,1::2]
        for i,layer in enumerate(self.layers):
            x=layer(x)
            if str(i) in self.norms:x=F.relu(self.norms[str(i)](x))
        return x.repeat_interleave(2,dim=2)

def initialize(network,saved):
    """Transfer96 features; new hop and temporal kernel require refitting."""
    state=network.state_dict();index=torch.arange(256)%96;counts=torch.bincount(index,minlength=96)
    for key,value in saved['state_dict'].items():
        if key.startswith('layers.') and key.endswith('weight'):
            layer=int(key.split('.')[1])
            if layer==11:continue
            v=value[index]
            if layer and not layer%2:v=v[:,index]/counts[index][None,:,None]
            if layer==9:v=v.sum(-1,keepdim=True)
            state[key]=v+torch.randn_like(v)*float(v.std())*.01
        elif key.startswith('norms.'):
            state[key]=value[index] if value.ndim else value.clone()
    network.load_state_dict(state)
    with torch.no_grad():
        torch.nn.init.normal_(network.layers[-1].conv.weight,std=.01);network.layers[-1].conv.bias.zero_()

def score(logits):return logits[:,1:].max(1,keepdim=True).values-logits[:,:1]
