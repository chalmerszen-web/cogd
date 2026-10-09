"""Untrained GRU64 host contract; static tagged Q8/Q15 C11 export only."""
import numpy as np
import torch
from torch import nn

ABI = 0x01343647
CONTRACT = 'xiaoyan_gru64_reset_after_q8_q15_event2_v1'
FIELDS = (('input', (3, 64, 40), 'int8'), ('recurrent', (3, 64, 64), 'int8'),
          ('input_bias', (3, 64), 'int16'), ('recurrent_bias', (3, 64), 'int16'),
          ('output', (2, 64), 'int8'), ('output_bias', (2,), 'int16'))


class Recurrent64(nn.Module):
    def __init__(self):
        super().__init__()
        self.recurrent = nn.GRU(40, 64, batch_first=True)
        self.output = nn.Linear(64, 2)

    def forward(self, values, state=None):
        if (values.ndim != 3 or values.shape[1] != 40 or not values.shape[0] or
                not values.shape[2] or not values.is_floating_point()):
            raise ValueError('Expected nonempty batch x40 x frames normalized values')
        hidden, state = self.recurrent(values.transpose(1, 2), state)
        return self.output(hidden).transpose(1, 2), state


def quantize(network):
    """Frozen exact-shape Q8 export; never clamp out-of-range coefficients."""
    if type(network) is not Recurrent64 or network.training:
        raise ValueError('Frozen exact GRU64 required')
    parameters = (network.recurrent.weight_ih_l0, network.recurrent.weight_hh_l0,
                  network.recurrent.bias_ih_l0, network.recurrent.bias_hh_l0,
                  network.output.weight, network.output.bias)
    result = {}
    for (name, shape, dtype), parameter in zip(FIELDS, parameters):
        if parameter.numel() != int(np.prod(shape)):
            raise ValueError('Wrong GRU64 coefficient count: ' + name)
        value = parameter.detach().cpu().numpy().astype(np.float64).reshape(shape) * 256
        if not np.isfinite(value).all():
            raise ValueError('Nonfinite parameter')
        integer = np.sign(value) * np.floor(np.abs(value) + .5)
        limits = np.iinfo(dtype)
        if np.any((integer < limits.min) | (integer > limits.max)):
            raise ValueError('Parameter outside fixed quantized range: ' + name)
        result[name] = np.ascontiguousarray(integer, dtype=dtype)
    validate(result)
    return result


def validate(bundle):
    if not isinstance(bundle, dict) or set(bundle) != {field[0] for field in FIELDS}:
        raise ValueError('Unknown GRU64 weight fields')
    for name, shape, dtype in FIELDS:
        value = bundle[name]
        if (not isinstance(value, np.ndarray) or value.shape != shape or
                value.dtype != np.dtype(dtype) or not value.flags.c_contiguous):
            raise ValueError('Invalid GRU64 field: ' + name)


def emit_c(bundle, symbol='kws_gru64_candidate'):
    """Tagged static weights. Activation requires a separate qualified manifest."""
    validate(bundle)
    if not isinstance(symbol, str) or not symbol.isascii() or not symbol.isidentifier():
        raise ValueError('Invalid C symbol')

    def braces(value):
        if value.ndim == 1:
            return '{' + ','.join(map(str, value.tolist())) + '}'
        return '{' + ','.join(braces(row) for row in value) + '}'

    fields = ',\n'.join('    .' + name + '=' + braces(bundle[name]) for name, _, _ in FIELDS)
    return ('#include "kws_gru64.h"\nconst kws_gru64_model_t ' + symbol +
            '={\n    .abi=KWS_GRU64_ABI,\n' + fields + '\n};\n')
