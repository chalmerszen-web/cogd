"""One causal48 CTC14 model; PyTorch is host-only, deployment is integer C11."""
import math
import numpy as np
import torch
from torch.nn import functional as F
from model import Model, Causal
from quantize import fold_batch_norm
from export_c import array
from ctc_data import TOKENS

CONTRACT = 'causal48_ctc14_stride512_complete_sources_v1'


class CTCModel(Model):
    def __init__(self):
        super().__init__(48)
        self.layers[-1] = Causal(48, len(TOKENS), 1, bias=True)


def loss(logits, targets):
    if (logits.ndim != 3 or logits.shape[1] != len(TOKENS) or
            logits.shape[-1] % 2 or not torch.isfinite(logits).all()):
        raise ValueError('Expected finite batch x14 x even16ms frames')
    if (targets.shape != (len(logits), 4) or targets.dtype != torch.int64 or
            bool(((targets <= 0) | (targets >= len(TOKENS))).any())):
        raise ValueError('Expected four nonblank integer target tokens per row')
    frames = logits.shape[-1] // 2
    required = 4 + (targets[:, 1:] == targets[:, :-1]).sum(1)
    if bool((required > frames).any()):
        raise ValueError('Insufficient CTC frames for repeated labels')
    log_probs = F.log_softmax(logits[:, :, 1::2], 1).permute(2, 0, 1).contiguous()
    return F.ctc_loss(log_probs, targets,
        torch.full((len(targets),), frames, dtype=torch.int64),
        torch.full((len(targets),), 4, dtype=torch.int64),
        blank=0, reduction='mean', zero_infinity=False)


def blank_loss(logits):
    """Empty-transcript CTC on proven silence, normalized by frame count.

    This is training supervision, not a runtime mute or a phonetic annotation
    for arbitrary human speech. The caller must supply genuine silence.
    """
    if (logits.ndim != 3 or not len(logits) or logits.shape[1] != len(TOKENS) or
            logits.shape[-1] < 2 or logits.shape[-1] % 2 or
            not torch.isfinite(logits).all()):
        raise ValueError('Expected finite nonempty batch x14 x even16ms frames')
    frames = logits.shape[-1] // 2
    probabilities = F.log_softmax(logits[:, :, 1::2], 1).permute(2, 0, 1).contiguous()
    objectives = F.ctc_loss(probabilities, torch.empty(0, dtype=torch.int64),
        torch.full((len(logits),), frames, dtype=torch.int64),
        torch.zeros(len(logits), dtype=torch.int64), blank=0,
        reduction='none', zero_infinity=False)
    return objectives.mean() / frames


def collapse(labels):
    result, previous = [], None
    for label in labels:
        label = int(label)
        if label != previous and label != 0:
            result.append(label)
        previous = label
    return result


def integer_logits(value):
    """Explicit contiguous frame x14 ABI for C; transposed NumPy rows aren't."""
    value = np.asarray(value)
    if value.ndim != 2 or value.shape[1] != len(TOKENS) or not np.isfinite(value).all():
        raise ValueError('Expected finite frame x14 class logits')
    scaled = value.astype(np.float64) * 256
    rounded = np.sign(scaled) * np.floor(np.abs(scaled) + .5)
    return np.ascontiguousarray(rounded.clip(-32768, 32767), dtype=np.int16)


@torch.no_grad()
def quantize_model(network, values, normalization):
    """Single TRAIN calibration; last14 class layer stays in the C artifact."""
    network.eval()
    value = values
    rows, input_exp = [], -5
    for i, layer in enumerate(network.layers):
        weight = layer.conv.weight.detach().cpu().numpy().astype(np.float64)
        bias = (np.zeros(len(weight)) if layer.conv.bias is None else
                layer.conv.bias.detach().cpu().numpy().astype(np.float64))
        if str(i) in network.norms:
            bn = network.norms[str(i)]
            weight, bias = fold_batch_norm(weight, bias, bn.weight.detach().cpu().numpy(),
                bn.bias.detach().cpu().numpy(), bn.running_mean.cpu().numpy(),
                bn.running_var.cpu().numpy(), bn.eps)
        value = layer(value)
        if str(i) in network.norms:
            value = F.relu(network.norms[str(i)](value))
        maximum = float(value.abs().max())
        output_exp = -8 if i == 11 else math.ceil(math.log2(max(maximum, 1e-9) / 127))
        weight_exp = np.ceil(np.log2(np.maximum(np.abs(weight).max((1, 2)), 1e-9) / 127)).astype(int)
        round_away = lambda x: np.sign(x) * np.floor(np.abs(x) + .5)
        integer_w = np.clip(round_away(weight / np.exp2(weight_exp[:, None, None])), -127, 127).astype(np.int8)
        integer_b = round_away(bias / np.exp2(input_exp + weight_exp)).astype(np.int64)
        shift = output_exp - input_exp - weight_exp
        depthwise = 0 < i < 11 and i % 2 == 1
        margin = weight.shape[1] * weight.shape[2] * 16384
        if (np.any(np.abs(shift) > 31) or np.any(np.abs(integer_b) > 2147483647 - margin)
                or (depthwise and np.any(integer_b))):
            raise ValueError('Unsupported integer shift, bias or accumulation')
        rows.append(dict(inputs=layer.conv.in_channels, outputs=layer.conv.out_channels,
            kernel=weight.shape[2], dilation=layer.conv.dilation[0], depthwise=int(depthwise),
            relu=int(i < 11 and not depthwise),
            weights=integer_w.transpose(0, 2, 1).flatten().astype(int).tolist(),
            bias=integer_b.tolist(), shift=shift.tolist(), input_exp=input_exp,
            output_exp=output_exp, calibration_abs_max=maximum))
        input_exp = output_exp
    # The old ABI may validate this backbone, but cannot declare a scalar wake.
    head = rows.pop()
    rows.append(dict(inputs=48, outputs=1, kernel=1, dilation=1, depthwise=0,
        relu=0, weights=[0] * 48, bias=[0], shift=[0]))
    backbone = dict(name='xiaoyan_ctc48_features_v1', channels=48, trained=False,
        mean_q8=normalization['mean_q8'], inverse_std_q12=normalization['inverse_std_q12'],
        layers=rows)
    return dict(contract=CONTRACT, backbone=backbone, token_head=head, tokens=list(TOKENS),
        scalar_ABI_untrained_and_unused=True, token_head_retained=True,
        output_stride_samples=512, post_training_calibrations=1)


def emit_c(bundle):
    """Emit a feature-only legacy descriptor plus a distinct retained14 head."""
    if bundle['contract'] != CONTRACT or bundle['tokens'] != list(TOKENS):
        raise ValueError('Unknown token model ABI')
    base = bundle['backbone']
    parts = ['#include "kws_ctc_head.h"\n',
             '_Static_assert(KWS_CHANNELS == 48, "CTC backbone requires48");\n']
    for i, row in enumerate(base['layers']):
        parts.append(array('int8_t', f'w{i}', row['weights']))
        if not row['depthwise']:
            parts.append(array('int32_t', f'b{i}', row['bias']))
        parts.append(array('int8_t', f's{i}', row['shift']))
    parts.extend((array('int16_t', 'mean', base['mean_q8']),
                  array('uint16_t', 'inverse_std', base['inverse_std_q12'])))
    parts.append('static const kws_layer_t layers[12]={\n')
    for i, row in enumerate(base['layers']):
        bias = 'NULL' if row['depthwise'] else f'b{i}'
        fields = ','.join(str(row[k]) for k in ('inputs', 'outputs', 'kernel', 'dilation', 'depthwise', 'relu'))
        parts.append(f'{{w{i},{bias},s{i},{fields}}},\n')
    parts.append('};\nconst kws_model_t kws_probe_model={"' + base['name'] +
                 '",layers,mean,inverse_std,false};\n')
    head = bundle['token_head']
    for key, ctype in (('weights', 'int8_t'), ('bias', 'int32_t'), ('shift', 'int8_t')):
        parts.append(array(ctype, 'ctc_' + key, head[key]))
    parts.append('const kws_layer_t kws_ctc_token_layer={ctc_weights,ctc_bias,ctc_shift,48,14,1,1,0,0};\n')
    return ''.join(parts)
