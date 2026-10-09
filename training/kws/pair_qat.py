"""Host-only QAT for the bounded, frozen dual-branch C11 inference contract."""
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

MIXED=(2,4,6,8,10)

def round_away(value):
    return value.sign()*torch.floor(value.abs()+.5)

def straight_integer(value,rounded):
    return value+(rounded-value).detach()

class Fixed(nn.Module):
    def __init__(self,metadata,index):
        super().__init__()
        self.inputs,self.outputs,self.kernel,self.dilation=(metadata[key] for key in
            ('inputs','outputs','kernel','dilation'))
        self.depthwise,self.relu=bool(metadata['depthwise']),bool(metadata['relu'])
        self.final=index==11
        raw=torch.tensor(metadata['weights'],dtype=torch.float32)
        weight=raw.reshape(self.outputs,1,self.kernel) if self.depthwise else \
            raw.reshape(self.outputs,self.kernel,self.inputs).transpose(1,2).contiguous()
        self.register_buffer('weight',weight)
        self.register_buffer('bias',torch.tensor(metadata['bias'],dtype=torch.float32))
        self.register_buffer('scale',torch.pow(2.,torch.tensor(metadata['shift'],dtype=torch.float32))[None,:,None])
        margin=self.kernel*(1 if self.depthwise else self.inputs)*16384
        if index in MIXED:margin+=24*16384
        self.maximum_partial_sum=margin+max(abs(value) for value in metadata['bias'])
        if self.maximum_partial_sum>2**24:
            raise ValueError('Float32 exact-integer convolution bound exceeded')

    def accumulate(self,values):
        return F.conv1d(F.pad(values,(self.dilation*(self.kernel-1),0)),self.weight,self.bias,
            dilation=self.dilation,groups=self.inputs if self.depthwise else 1)

    def quantize(self,accumulator):
        value=accumulator/self.scale
        value=straight_integer(value,round_away(value))
        return value.clamp(-32768 if self.final else 0 if self.relu else -128,32767 if self.final else 127)

class PairedQAT(nn.Module):
    """Both inputs stay intact until both branch outputs have been computed."""
    def __init__(self,models):
        super().__init__()
        if len(models)!=2 or any(len(model['layers'])!=12 for model in models):
            raise ValueError('Expected original E/L topology')
        if models[0]['mean']!=models[1]['mean'] or models[0]['inverse']!=models[1]['inverse']:
            raise ValueError('Shared frontend normalization differs')
        self.branches=nn.ModuleList([nn.ModuleList([Fixed(layer,i) for i,layer in
            enumerate(model['layers'])]) for model in models])
        self.cross=nn.Parameter(torch.zeros(5,2,24,24))
        self.metadata=models

    def forward(self,features,trace=False):
        if features.ndim!=3 or features.shape[1]!=40:
            raise ValueError('Expected batch x40 x256-sample frames')
        weight=self.cross.clamp(-128,127)
        weight=straight_integer(weight,round_away(weight))
        inputs=[features,features];traces=[[],[]];slot=0
        for index in range(12):
            outputs=[]
            for branch in (0,1):
                layer=self.branches[branch][index]
                accumulator=layer.accumulate(inputs[branch])
                if index in MIXED:
                    accumulator=accumulator+F.conv1d(inputs[1-branch],weight[slot,branch,:,:,None])
                output=layer.quantize(accumulator);outputs.append(output)
                if trace:traces[branch].append(output)
            inputs=outputs
            if index in MIXED:slot+=1
        return torch.stack([torch.cat(values,dim=1) for values in traces],dim=1) if trace \
            else torch.stack([value[:,0,:] for value in inputs],dim=1)

    def export(self):
        return round_away(self.cross.detach().clamp(-128,127)).to(torch.int8).cpu().numpy()

def smooth_tensor(heads):
    """Exact C truncation: average heads at odd frame, then three-block mean."""
    joint=heads[:,:,1::2].sum(dim=1)/2
    joint=straight_integer(joint,torch.trunc(joint))
    sums=F.conv1d(F.pad(joint[:,None,:],(2,0)),joint.new_ones(1,1,3))[:,0,:]
    divisor=torch.arange(1,joint.shape[-1]+1,dtype=joint.dtype,device=joint.device).clamp_max(3)
    value=sums/divisor
    return straight_integer(value,torch.trunc(value))

def smooth_numpy(e,l):
    joint=e.astype(np.int32)+l.astype(np.int32)
    joint=np.sign(joint)*(np.abs(joint)//2)
    padded=np.pad(joint,((0,0),(2,0)))
    sums=padded[:,:-2]+padded[:,1:-1]+padded[:,2:]
    count=np.minimum(3,np.arange(1,joint.shape[-1]+1))
    return (np.sign(sums)*(np.abs(sums)//count)).astype(np.int16)

def events(scores):
    """Cold, contiguous512-sample original detector; stable mode is disabled."""
    votes=0;cooldown=0;result=[]
    for block,score in enumerate(scores,1):
        end=block*512;votes=((votes<<1)|int(score>=268))&7
        if block>=64 and end>=cooldown and votes.bit_count()>=2:
            votes=0;cooldown=end+24000;result.append(end)
    return result

def integer_heads(features,models,cross):
    """Separate float64 Conv1d integer oracle without STE or training buffers."""
    inputs=[torch.as_tensor(features,dtype=torch.float64).transpose(1,2)]*2
    slot=0
    for index in range(12):
        outputs=[]
        for branch in (0,1):
            layer=models[branch]['layers'][index]
            oc,ic,k,d=(layer[key] for key in ('outputs','inputs','kernel','dilation'))
            raw=torch.tensor(layer['weights'],dtype=torch.float64)
            weight=raw.reshape(oc,1,k) if layer['depthwise'] else raw.reshape(oc,k,ic).transpose(1,2)
            accumulator=F.conv1d(F.pad(inputs[branch],(d*(k-1),0)),weight,
                torch.tensor(layer['bias'],dtype=torch.float64),dilation=d,groups=ic if layer['depthwise'] else 1)
            if index in MIXED:
                accumulator+=F.conv1d(inputs[1-branch],torch.tensor(cross[slot,branch,:,:,None],dtype=torch.float64))
            scale=torch.pow(2.,torch.tensor(layer['shift'],dtype=torch.float64))[None,:,None]
            output=torch.sign(accumulator)*torch.floor(torch.abs(accumulator)/scale+.5)
            output=output.clamp(0 if layer['relu'] else -32768 if index==11 else -128,
                32767 if index==11 else 127)
            outputs.append(output)
        inputs=outputs
        if index in MIXED:slot+=1
    return torch.stack([value[:,0,:] for value in inputs],dim=1).to(torch.int16)
