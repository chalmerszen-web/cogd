"""Explicit tagged 64/Q6 matrix, Q8 bias/logit, Q15 state host export."""
from dataclasses import dataclass
import numpy as np
from recurrent64 import Recurrent64, FIELDS, validate

CONTRACT = 'xiaoyan_gru64_reset_after_q6_q8_q15_event2_v1'
ABI = 0x06343647


@dataclass(frozen=True)
class Q6Weights:
    values: dict
    contract: str = CONTRACT


def quantize_q6(network):
    if type(network) is not Recurrent64 or network.training:
        raise ValueError('Frozen exact GRU64 required')
    parameters = (network.recurrent.weight_ih_l0, network.recurrent.weight_hh_l0,
                  network.recurrent.bias_ih_l0, network.recurrent.bias_hh_l0,
                  network.output.weight, network.output.bias)
    result = {}
    for (name, shape, dtype), parameter in zip(FIELDS, parameters):
        if parameter.numel() != int(np.prod(shape)):
            raise ValueError('Wrong GRU64 coefficient count: ' + name)
        value = parameter.detach().cpu().numpy().astype(np.float64).reshape(shape)
        value *= 64 if dtype == 'int8' else 256
        if not np.isfinite(value).all():
            raise ValueError('Nonfinite parameter')
        integer = np.sign(value) * np.floor(np.abs(value) + .5)
        limits = np.iinfo(dtype)
        if np.any((integer < limits.min) | (integer > limits.max)):
            raise ValueError('Parameter outside Q6 matrix/Q8 bias range: ' + name)
        result[name] = np.ascontiguousarray(integer, dtype=dtype)
    validate(result)
    return Q6Weights(result)


def emit_q6_c(bundle, symbol='kws_gru64_q6_candidate'):
    if type(bundle) is not Q6Weights or bundle.contract != CONTRACT:
        raise ValueError('Explicit 64/Q6 weight bundle required')
    validate(bundle.values)
    if not isinstance(symbol, str) or not symbol.isascii() or not symbol.isidentifier():
        raise ValueError('Invalid C symbol')

    def braces(value):
        if value.ndim == 1:
            return '{' + ','.join(map(str, value.tolist())) + '}'
        return '{' + ','.join(braces(row) for row in value) + '}'

    fields = ',\n'.join('    .' + name + '=' + braces(bundle.values[name]) for name, _, _ in FIELDS)
    return ('#include "kws_gru64_q6.h"\nconst kws_gru64_q6_model_t ' + symbol +
            '={\n .abi=KWS_GRU64_Q6_ABI,\n' + fields + '\n};\n')
