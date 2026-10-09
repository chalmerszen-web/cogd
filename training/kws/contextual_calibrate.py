"""TRAIN-only PTQ using the verified continuous contextual input contract."""
from pathlib import Path
import hashlib
import json
import math
import numpy as np
import torch
from conditioned import checkpoint_conditioned
from contextual import contextual_forward
from calibrate import calibration_indices, round_away
from quantize import fold_batch_norm


def calibrate_contextual(checkpoint, features, corpus, maximum=512):
    return _calibrate_contextual(checkpoint, features, corpus, maximum,
                                checkpoint_conditioned, contextual_forward)


def _calibrate_contextual(checkpoint, features, corpus, maximum, factory, forward):
    saved = torch.load(checkpoint, map_location='cpu', weights_only=True)
    config = saved['config']
    if config.get('context_input_contract') != 'left126_continuous48_maskedBN_v1':
        raise ValueError('Unregistered contextual calibration input')
    for name, digest in config['feature_hashes'].items():
        if hashlib.sha256((features/name).read_bytes()).hexdigest() != digest:
            raise ValueError('Changed calibration input: '+name)
    if json.loads((features/'normalization.json').read_text()) != saved['normalization']:
        raise ValueError('Changed frontend normalization')
    net = factory(saved).eval()
    net.load_state_dict(saved['state_dict'])
    with np.load(features/'train.npz', allow_pickle=False) as data:
        if corpus.raw.shape != data['x'].shape or not np.array_equal(corpus.raw, data['x']):
            raise ValueError('Contextual corpus differs from frozen TRAIN data')
        indices = calibration_indices(np.random.default_rng(config['seed']), len(data['x']), maximum,
            data['device_domain'] if 'device_domain' in data else None)
        clip_ids = data['clip_id'][indices].tolist()
        device_count = int(data['device_domain'][indices].sum()) if 'device_domain' in data else 0
    joined, active = corpus.batch(indices)
    x = torch.from_numpy(joined.astype(np.float32).transpose(0, 2, 1)/32)
    mask = torch.from_numpy(active)
    with torch.no_grad():
        _, traces = forward(net, x, mask, trace=True)
        ranges = [float(value.abs().max()) for value in traces]
    folded = []
    for i, layer in enumerate(net.layers):
        weight = layer.conv.weight.detach().numpy().astype(np.float64)
        bias = np.zeros(weight.shape[0]) if layer.conv.bias is None else layer.conv.bias.detach().numpy().astype(np.float64)
        if str(i) in net.norms:
            bn = net.norms[str(i)]
            weight, bias = fold_batch_norm(weight, bias, bn.weight.detach().numpy(), bn.bias.detach().numpy(),
                bn.running_mean.numpy(), bn.running_var.numpy(), bn.eps)
        folded.append((weight, bias))
    layers = []
    input_exp = -5
    for i, ((weight, bias), maximum_value) in enumerate(zip(folded, ranges)):
        depthwise = 0 < i < 11 and i % 2 == 1
        output_exp = -8 if i == 11 else math.ceil(math.log2(max(maximum_value, 1e-9)/127))
        weight_exp = np.ceil(np.log2(np.maximum(np.max(np.abs(weight), axis=(1, 2)), 1e-9)/127)).astype(int)
        integer_weight = np.clip(round_away(weight/np.exp2(weight_exp[:, None, None])), -127, 127).astype(np.int8)
        integer_bias = round_away(bias/np.exp2(input_exp+weight_exp)).astype(np.int64)
        shift = output_exp-input_exp-weight_exp
        margin = weight.shape[1]*weight.shape[2]*16384
        if np.any(np.abs(shift) > 31) or np.any(np.abs(integer_bias) > 2147483647-margin):
            raise ValueError('Unrepresentable INT8 layer')
        if depthwise and np.any(integer_bias):
            raise ValueError('Unexpected depthwise bias')
        layers.append(dict(inputs=net.layers[i].conv.in_channels, outputs=net.layers[i].conv.out_channels,
            kernel=weight.shape[2], dilation=net.layers[i].conv.dilation[0], depthwise=int(depthwise),
            relu=int(i < 11 and not depthwise),
            weights=integer_weight.transpose(0, 2, 1).flatten().astype(int).tolist(),
            bias=integer_bias.tolist(), shift=shift.tolist(), input_exp=input_exp, output_exp=output_exp,
            weight_exp=weight_exp.tolist(), calibration_abs_max=maximum_value))
        input_exp = output_exp
    return dict(name=config['topology'].removesuffix('_v1')+'_context_v1', trained=True, channels=24,
        input_features=net.layers[0].conv.in_channels, topology=config['topology'],
        seed=config['seed'], **saved['normalization'], layers=layers,
        checkpoint_sha256=hashlib.sha256(Path(checkpoint).read_bytes()).hexdigest(),
        feature_hashes=config['feature_hashes'], frozen_hidden_sha256=config['frozen_hidden_sha256'],
        frozen_models=config['frozen_models'], context_input_contract=config['context_input_contract'],
        parent_hidden_sha256=config['parent_hidden_sha256'], calibration_split='train',
        calibration_indices=indices.tolist(), calibration_clip_ids=clip_ids,
        calibration_device_count=device_count, training_step=saved['step'])
