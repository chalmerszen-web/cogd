"""Host-only QAT and independent integer oracle for a frozen-feature head.

48 E/L INT8 features -> 16 clipped ReLU values -> one Q8.8 score. The C11
prototype has fixed shifts8/3, no BN, interpreter, recurrent state, or heap.
This module is optional and does not change the default trainer.
"""
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

BIAS_LIMIT = 1048576


def rounded(value):
    integer = value.sign() * (value.abs() + .5).floor()
    return value + (integer - value).detach()


class VerifyHead(nn.Module):
    def __init__(self):
        super().__init__()
        self.hidden = nn.Linear(48, 16)
        self.output = nn.Linear(16, 1)

    def forward(self, features):
        if features.ndim != 3 or features.shape[1] != 48:
            raise ValueError('Expected batch x48 x time frozen INT8 feature values')
        x = features.transpose(1, 2).to(dtype=self.hidden.weight.dtype)
        weight = rounded(self.hidden.weight * 64).clamp(-128, 127)
        bias = rounded(self.hidden.bias * 8192).clamp(-BIAS_LIMIT, BIAS_LIMIT)
        acc = F.linear(x, weight, bias)
        hidden = rounded(acc / 256).clamp(0, 127)
        weight = rounded(self.output.weight * 64).clamp(-128, 127)
        bias = rounded(self.output.bias * 2048).clamp(-BIAS_LIMIT, BIAS_LIMIT)
        score = rounded(F.linear(hidden, weight, bias) / 8).clamp(-32768, 32767)
        return score.transpose(1, 2) / 256

    @torch.no_grad()
    def project(self):
        for layer, scale in ((self.hidden, 8192), (self.output, 2048)):
            layer.weight.clamp_(-2, 127 / 64)
            layer.bias.clamp_(-BIAS_LIMIT / scale, BIAS_LIMIT / scale)

    @torch.no_grad()
    def export(self):
        self.project()
        def integer(value, scale):
            return rounded(value * scale).to(torch.int32).cpu().tolist()
        return dict(format='kws_verify48x16_v1', inputs=48, hidden=16,
                    hidden_shift=8, output_shift=3, bias_limit=BIAS_LIMIT,
                    input_weight=integer(self.hidden.weight, 64),
                    hidden_bias=integer(self.hidden.bias, 8192),
                    output_weight=integer(self.output.weight, 64)[0],
                    output_bias=integer(self.output.bias, 2048)[0])


def integer_score(features, weights):
    """Independent int64 matrix sums and signed half-away requantization."""
    x = np.asarray(features)
    if x.ndim < 1 or x.shape[-1] != 48 or x.dtype != np.int8:
        raise ValueError('Expected INT8 features with final dimension48')
    if (weights.get('format') != 'kws_verify48x16_v1' or weights['inputs'] != 48
            or weights['hidden'] != 16 or weights['hidden_shift'] != 8
            or weights['output_shift'] != 3 or weights['bias_limit'] != BIAS_LIMIT):
        raise ValueError('Unregistered head format')
    first = np.asarray(weights['input_weight'], dtype=np.int64)
    bias = np.asarray(weights['hidden_bias'], dtype=np.int64)
    last = np.asarray(weights['output_weight'], dtype=np.int64)
    final_bias = weights['output_bias']
    if (first.shape != (16, 48) or bias.shape != (16,) or last.shape != (16,)
            or (np.abs(bias) > BIAS_LIMIT).any() or abs(final_bias) > BIAS_LIMIT
            or (first < -128).any() or (first > 127).any()
            or (last < -128).any() or (last > 127).any()):
        raise ValueError('Invalid bounded head weights')
    acc = x.astype(np.int64) @ first.T + bias
    hidden = np.clip(np.sign(acc) * ((np.abs(acc) + 128) // 256), 0, 127)
    acc = hidden @ last + final_bias
    return np.clip(np.sign(acc) * ((np.abs(acc) + 4) // 8), -32768, 32767).astype(np.int16)


def emit_c(weights):
    integer_score(np.zeros((1, 48), dtype=np.int8), weights)
    array = lambda values: '{' + ','.join(str(int(value)) for value in values) + '}'
    rows = ',\n'.join('    ' + array(row) for row in weights['input_weight'])
    return ('#include "kws_verify_head.h"\n'
            'const kws_verify_weights_t kws_verify_trained = {\n'
            '  .input_weight = {\n' + rows + '\n  },\n'
            '  .hidden_bias = ' + array(weights['hidden_bias']) + ',\n'
            '  .output_weight = ' + array(weights['output_weight']) + ',\n'
            '  .output_bias = ' + str(weights['output_bias']) + '\n};\n')
