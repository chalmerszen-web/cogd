"""Fixed 24/48-channel causal DS-TCN; PyTorch runs only on the training host."""
import torch
from torch import nn
from torch.nn import functional as F

class Causal(nn.Module):
    def __init__(self, inputs, outputs, kernel, dilation=1, groups=1, bias=False):
        super().__init__()
        self.past=(kernel-1)*dilation
        self.conv=nn.Conv1d(inputs,outputs,kernel,dilation=dilation,groups=groups,bias=bias)
    def forward(self,x): return self.conv(F.pad(x,(self.past,0)))
    def step(self,x,state):
        if not self.past: return self.conv(x),None
        if state is None: state=x.new_zeros(x.shape[0],x.shape[1],self.past)
        joined=torch.cat((state,x),dim=-1)
        return self.conv(joined),joined[:,:,-self.past:]

class Model(nn.Module):
    def __init__(self, channels=24):
        super().__init__()
        if channels not in (24,48): raise ValueError('Only registered 24/48-channel host topologies are supported')
        self.channels=channels
        layers=[Causal(40,channels,3)]
        for d in (1,2,4,8,16): layers.extend((Causal(channels,channels,5,d,channels),Causal(channels,channels,1)))
        layers.append(Causal(channels,1,1,bias=True))
        self.layers=nn.ModuleList(layers)
        self.norms=nn.ModuleDict({str(i):nn.BatchNorm1d(channels) for i in (0,2,4,6,8,10)})
    def forward(self,x):
        for i,layer in enumerate(self.layers):
            x=layer(x)
            if str(i) in self.norms: x=F.relu(self.norms[str(i)](x))
        return x
    def step(self,x,states=None):
        if self.training: raise RuntimeError('Streaming parity requires frozen BatchNorm')
        states=[None]*12 if states is None else states
        next_states=[]
        for i,layer in enumerate(self.layers):
            x,state=layer.step(x,states[i]); next_states.append(state)
            if str(i) in self.norms: x=F.relu(self.norms[str(i)](x))
        return x,next_states

def checkpoint_channels(saved):
    """Legacy checkpoints are24; new widths require explicit, consistent metadata."""
    channels=saved.get('config',{}).get('channels',24)
    if type(channels) is not int or channels not in (24,48):
        raise ValueError('Unregistered checkpoint width')
    stem=saved.get('state_dict',{}).get('layers.0.conv.weight')
    if stem is None or tuple(stem.shape)!=(channels,40,3):
        raise ValueError('Checkpoint width and stem tensor differ')
    return channels

def verify_streaming(channels=24):
    torch.manual_seed(20260921); torch.set_num_threads(1)
    model=Model(channels).eval()
    parameters=sum(p.numel() for p in model.parameters())
    assert parameters==5*channels**2+158*channels+1
    with torch.no_grad():
        x=torch.randn(2,40,256)
        expected=model(x)
        state=None; parts=[]
        for frame in x.split(1,dim=-1):
            out,state=model.step(frame,state); parts.append(out)
        actual=torch.cat(parts,dim=-1)
    torch.testing.assert_close(actual,expected,rtol=1e-5,atol=1e-7)
    return dict(parameters=parameters,frames=256,batch=2,max_abs_error=(actual-expected).abs().max().item())

if __name__=='__main__': print(verify_streaming())
