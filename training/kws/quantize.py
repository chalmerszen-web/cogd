"""Integer oracle. Direct time-indexing intentionally differs from C ring buffers."""
import numpy as np

def requantize(value,shift,low=-128,high=127):
    value=np.asarray(value,dtype=np.int64)
    shift=np.asarray(shift,dtype=np.int64)
    right=np.maximum(shift,0)
    magnitude=(np.abs(value)+np.where(right>0,np.left_shift(1,np.maximum(right-1,0)),0))>>right
    result=np.where(value<0,-magnitude,magnitude)*np.left_shift(1,np.maximum(-shift,0))
    return np.clip(result,low,high).astype(np.int16)

def infer(features,model):
    x=np.asarray(features,dtype=np.int64)
    outputs=[]
    for layer in model['layers']:
        n=len(x); inputs=layer['inputs']; oc=layer['outputs']; k=layer['kernel']; d=layer['dilation']
        padded=np.pad(x,((d*(k-1),0),(0,0)))
        slices=np.stack([padded[t*d:t*d+n] for t in range(k)],axis=1)
        if layer['depthwise']:
            weights=np.array(layer['weights']).reshape(oc,k)
            acc=np.einsum('nki,ik->ni',slices,weights)
        else:
            weights=np.array(layer['weights']).reshape(oc,k,inputs)
            acc=np.einsum('nki,oki->no',slices,weights)
        acc+=np.array(layer['bias'])
        x=requantize(acc,layer['shift'],0 if layer['relu'] else -32768 if oc==1 else -128,32767 if oc==1 else 127)
        outputs.append(x)
    return np.concatenate(outputs,axis=1)

def fold_batch_norm(weight,bias,gamma,beta,mean,variance,epsilon):
    factor=gamma/np.sqrt(variance+epsilon)
    shape=(len(factor),)+(1,)*(weight.ndim-1)
    return weight*factor.reshape(shape),(bias-mean)*factor+beta

def torch_integer_oracle(features,model):
    """Validate C export layout against Conv1d, with exact float64 int sums.

    Exporters must transpose PyTorch [out,in,time] to C [out,time,in].
    This is a host-only oracle, not an embedded floating-point runtime.
    """
    import torch
    from torch.nn import functional as F
    torch.set_num_threads(1)
    x=torch.tensor(np.asarray(features).T[None],dtype=torch.float64)
    traces=[]
    for layer in model['layers']:
        oc,ic,k,d=(layer[n] for n in ('outputs','inputs','kernel','dilation'))
        raw=torch.tensor(layer['weights'],dtype=torch.float64)
        weights=raw.reshape(oc,1,k) if layer['depthwise'] else raw.reshape(oc,k,ic).transpose(1,2)
        x=F.conv1d(F.pad(x,(d*(k-1),0)),weights,torch.tensor(layer['bias'],dtype=torch.float64),dilation=d,groups=ic if layer['depthwise'] else 1)
        scale=torch.pow(2.,torch.tensor(layer['shift'],dtype=torch.float64))[None,:,None]
        x=torch.sign(x)*torch.floor(torch.abs(x)/scale+.5)
        x=torch.clamp(x,0 if layer['relu'] else -32768 if oc==1 else -128,32767 if oc==1 else 127)
        traces.append(x[0].T.numpy().astype(np.int16))
    return np.concatenate(traces,axis=1)
