"""Optional TRAIN-only PTQ of the88-input conditioned model; original exporter stays unchanged."""
from pathlib import Path
import hashlib
import json
import math
import numpy as np
import torch
from torch.nn import functional as F
from conditioned import checkpoint_conditioned
from calibrate import calibration_indices, round_away
from quantize import fold_batch_norm


def calibrate_conditioned(checkpoint, features, hidden_path, maximum=512):
    saved=torch.load(checkpoint,map_location='cpu',weights_only=True)
    for name,digest in saved['config']['feature_hashes'].items():
        if hashlib.sha256((features/name).read_bytes()).hexdigest()!=digest:
            raise ValueError('Changed calibration input: '+name)
    if hashlib.sha256(hidden_path.read_bytes()).hexdigest()!=saved['config']['frozen_hidden_sha256']:
        raise ValueError('Changed frozen hidden features')
    if json.loads((features/'normalization.json').read_text())!=saved['normalization']:
        raise ValueError('Changed frontend normalization')
    net=checkpoint_conditioned(saved).eval();net.load_state_dict(saved['state_dict'])
    data=np.load(features/'train.npz',allow_pickle=False)
    hidden=np.load(hidden_path,mmap_mode='r')
    if hidden.dtype!=np.int8 or hidden.shape!=(*data['x'].shape[:2],48):
        raise ValueError('Frozen feature cache shape or type differs')
    indices=calibration_indices(np.random.default_rng(saved['config']['seed']),len(data['x']),maximum,
                                data['device_domain'] if 'device_domain' in data else None)
    joined=np.concatenate((data['x'][indices],hidden[indices]),axis=2)
    x=torch.from_numpy(joined.astype(np.float32).transpose(0,2,1)/32)
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
    for i,((w,b),maximum_value) in enumerate(zip(folded,ranges)):
        dw=0<i<11 and i%2==1
        output_exp=-8 if i==11 else math.ceil(math.log2(max(maximum_value,1e-9)/127))
        weight_exp=np.ceil(np.log2(np.maximum(np.max(np.abs(w),axis=(1,2)),1e-9)/127)).astype(int)
        integer_w=np.clip(round_away(w/np.exp2(weight_exp[:,None,None])),-127,127).astype(np.int8)
        integer_b=round_away(b/np.exp2(input_exp+weight_exp)).astype(np.int64)
        shift=output_exp-input_exp-weight_exp
        margin=w.shape[1]*w.shape[2]*16384
        if np.any(np.abs(shift)>31) or np.any(np.abs(integer_b)>2147483647-margin):
            raise ValueError('Unrepresentable INT8 layer')
        if dw and np.any(integer_b):raise ValueError('Unexpected depthwise bias')
        layers.append(dict(inputs=net.layers[i].conv.in_channels,outputs=net.layers[i].conv.out_channels,
                           kernel=w.shape[2],dilation=net.layers[i].conv.dilation[0],depthwise=int(dw),
                           relu=int(i<11 and not dw),weights=integer_w.transpose(0,2,1).flatten().astype(int).tolist(),
                           bias=integer_b.tolist(),shift=shift.tolist(),input_exp=input_exp,output_exp=output_exp,
                           weight_exp=weight_exp.tolist(),calibration_abs_max=maximum_value))
        input_exp=output_exp
    return dict(name='raw40_el48_causal24_v1',trained=True,channels=24,input_features=88,
                topology='raw40_el48_causal24_v1',seed=saved['config']['seed'],**saved['normalization'],layers=layers,
                checkpoint_sha256=hashlib.sha256(Path(checkpoint).read_bytes()).hexdigest(),
                feature_hashes=saved['config']['feature_hashes'],
                frozen_hidden_sha256=saved['config']['frozen_hidden_sha256'],
                frozen_models=saved['config']['frozen_models'],
                calibration_split='train',calibration_indices=indices.tolist(),
                calibration_clip_ids=data['clip_id'][indices].tolist(),
                calibration_device_count=int(data['device_domain'][indices].sum()) if 'device_domain' in data else 0,
                training_step=saved['step'])
