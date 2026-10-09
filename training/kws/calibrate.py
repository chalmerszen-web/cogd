"""Fold a real checkpoint, train-only calibration, export static integer layers."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import numpy as np
import torch
from torch.nn import functional as F
from model import Model,checkpoint_channels
from quantize import fold_batch_norm
from export_c import array

def round_away(value):
    return np.sign(value)*np.floor(np.abs(value)+.5)


def calibration_indices(rng,count,maximum,domain=None):
    size=min(count,maximum)
    if domain is None or not np.any(domain):return rng.choice(count,size,replace=False)
    device=np.flatnonzero(domain);original=np.flatnonzero(~domain)
    chosen=np.r_[rng.choice(device,min(len(device),size//2),replace=False),
                 rng.choice(original,min(len(original),size-size//2),replace=False)]
    if len(chosen)<size:
        available=np.setdiff1d(np.arange(count),chosen)
        chosen=np.r_[chosen,rng.choice(available,size-len(chosen),replace=False)]
    return chosen

def emit_c(bundle,symbol='kws_trained_model'):
    parts=['/* Generated from a trained checkpoint; see adjacent JSON hashes. */\n#include "kws.h"\n']
    channels=bundle['layers'][0]['outputs']
    if channels not in (24,48) or bundle.get('channels',channels)!=channels:
        raise ValueError('Export width and model metadata differ')
    if channels==48:
        parts.append('_Static_assert(KWS_CHANNELS==48,"Model requires48 channel kernel");\n')
    for i,layer in enumerate(bundle['layers']):
        parts.append(array('int8_t',f'w{i}',layer['weights']))
        if not layer['depthwise'] or any(layer['bias']):
            parts.append(array('int32_t',f'b{i}',layer['bias']))
        parts.append(array('int8_t',f's{i}',layer['shift']))
    parts.append(array('int16_t','mean',bundle['mean_q8']))
    parts.append(array('uint16_t','inverse_std',bundle['inverse_std_q12']))
    parts.append('static const kws_layer_t layers[12]={\n')
    for i,l in enumerate(bundle['layers']):
        bias='NULL' if l['depthwise'] and not any(l['bias']) else f'b{i}'
        parts.append('{'+f'w{i},{bias},s{i},'+','.join(str(l[k]) for k in ('inputs','outputs','kernel','dilation','depthwise','relu'))+'},\n')
    parts.append('};\nconst kws_model_t '+symbol+'={"'+bundle['name']+'",layers,mean,inverse_std,true};\n')
    return ''.join(parts)

def calibrate(checkpoint,features,max_clips=512):
    saved=torch.load(checkpoint,map_location='cpu',weights_only=True)
    hashes=saved['config'].get('feature_hashes',{})
    for name,expected in hashes.items():
        if hashlib.sha256((features/name).read_bytes()).hexdigest()!=expected:
            raise ValueError('Training/calibration input changed: '+name)
    if (features/'normalization.json').exists():
        if json.loads((features/'normalization.json').read_text())!=saved['normalization']:
            raise ValueError('Calibration frontend statistics differ from training')
    channels=checkpoint_channels(saved)
    net=Model(channels).eval();net.load_state_dict(saved['state_dict'])
    data=np.load(features/'train.npz',allow_pickle=False)
    rng=np.random.default_rng(saved['config']['seed'])
    indices=calibration_indices(rng,len(data['x']),max_clips,data['device_domain'] if 'device_domain' in data else None)
    x=torch.from_numpy(data['x'][indices].astype(np.float32).transpose(0,2,1)/32)
    folded=[]
    for i,layer in enumerate(net.layers):
        w=layer.conv.weight.detach().numpy().astype(np.float64)
        b=np.zeros(w.shape[0]) if layer.conv.bias is None else layer.conv.bias.detach().numpy().astype(np.float64)
        if str(i) in net.norms:
            bn=net.norms[str(i)]
            w,b=fold_batch_norm(w,b,bn.weight.detach().numpy(),bn.bias.detach().numpy(),
                                bn.running_mean.numpy(),bn.running_var.numpy(),bn.eps)
        folded.append((w,b))
    ranges=[]
    with torch.no_grad():
        for i,layer in enumerate(net.layers):
            x=layer(x)
            if str(i) in net.norms:x=F.relu(net.norms[str(i)](x))
            ranges.append(float(x.abs().max()))
    layers=[];input_exp=-5
    for i,((w,b),maximum) in enumerate(zip(folded,ranges)):
        depthwise=0<i<11 and i%2==1
        output_exp=-8 if i==11 else math.ceil(math.log2(max(maximum,1e-9)/127))
        weight_exp=np.ceil(np.log2(np.maximum(np.max(np.abs(w),axis=(1,2)),1e-9)/127)).astype(int)
        integer_w=np.clip(round_away(w/np.exp2(weight_exp[:,None,None])),-127,127).astype(np.int8)
        integer_b=round_away(b/np.exp2(input_exp+weight_exp)).astype(np.int64)
        shift=output_exp-input_exp-weight_exp
        if np.any(np.abs(shift)>31):raise ValueError('Unsupported quantization shift')
        margin=w.shape[1]*w.shape[2]*16384
        if np.any(np.abs(integer_b)>2147483647-margin):raise ValueError('Bias overflow')
        if depthwise and np.any(integer_b):raise ValueError('DW bias unsupported')
        layers.append(dict(inputs=net.layers[i].conv.in_channels,outputs=net.layers[i].conv.out_channels,
            kernel=w.shape[2],dilation=net.layers[i].conv.dilation[0],depthwise=int(depthwise),
            relu=int(i<11 and not depthwise),weights=integer_w.transpose(0,2,1).flatten().astype(int).tolist(),
            bias=integer_b.tolist(),shift=shift.tolist(),input_exp=input_exp,output_exp=output_exp,
            weight_exp=weight_exp.tolist(),calibration_abs_max=maximum))
        input_exp=output_exp
    frontend=saved['normalization'].get('frontend','logmel_v1')
    if frontend not in ('logmel_v1','pcen_v1'):raise ValueError('Unknown trained frontend')
    bundle=dict(name=f'xiaoyan_ds_tcn{channels}_pcen_v1' if frontend=='pcen_v1' else f'xiaoyan_ds_tcn{channels}_v1',trained=True,seed=saved['config']['seed'],
                **saved['normalization'],layers=layers,
                checkpoint_sha256=hashlib.sha256(Path(checkpoint).read_bytes()).hexdigest(),
                feature_hashes=hashes,
                calibration_split='train',calibration_clip_ids=data['clip_id'][indices].tolist(),
                calibration_device_count=int(data['device_domain'][indices].sum()) if 'device_domain' in data else 0,
                training_step=saved['step'])
    if channels!=24:bundle['channels']=channels
    return bundle

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--checkpoint',type=Path,required=True)
    ap.add_argument('--features',type=Path,required=True);ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--max-clips',type=int,default=512);args=ap.parse_args()
    torch.set_num_threads(4);args.out.mkdir(parents=True,exist_ok=False)
    bundle=calibrate(args.checkpoint,args.features,args.max_clips)
    (args.out/'model.json').write_text(json.dumps(bundle,separators=(',',':'))+'\n')
    (args.out/'model.c').write_text(emit_c(bundle),newline='\n')
    print(json.dumps(dict(name=bundle['name'],trained=True,checkpoint_sha256=bundle['checkpoint_sha256'],
                         model_sha256=hashlib.sha256((args.out/'model.c').read_bytes()).hexdigest())))

if __name__=='__main__': main()
