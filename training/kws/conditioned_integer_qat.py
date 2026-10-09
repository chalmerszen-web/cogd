"""Fixed-scale QAT for the exact90-input C11 graph, on the training host only.

Only integer acoustic weights can change. Biases, shifts, input normalization,
history, graph, and decision rules remain fixed. Every forward activation is a
C integer; straight-through differentiation is solely an optimization aid.
"""
from copy import deepcopy
import torch
from torch import nn
from pair_qat import round_away, straight_integer
from full_pair_qat import Trainable
from periodic import TOPOLOGY, ENCODING
from contextual import CONTEXT_FRAMES

CONTRACT = 'c_exact90_fixed_bias_shift_integer_weights_v1'


class IntegerLayer(Trainable):
    def quantize(self, accumulator):
        # Float32 convolution is integer-exact under the checked partial-sum
        # bound. Float64 rounding also handles large right shifts at half ties.
        scaled = accumulator/self.scale
        rounded = round_away(accumulator.detach().double()/self.scale.double()).to(scaled.dtype)
        return straight_integer(scaled, rounded).clamp(
            -32768 if self.final else 0 if self.relu else -128,
            32767 if self.final else 127)


class IntegerConditioned(nn.Module):
    def __init__(self, model):
        super().__init__()
        if (model.get('topology') != TOPOLOGY or model.get('pitch_encoding') != ENCODING
                or model.get('input_features') != 90 or len(model.get('layers', ())) != 12):
            raise ValueError('Expected the registered90-input periodic-strength graph')
        for index, row in enumerate(model['layers']):
            dw = bool(index and index < 11 and index % 2)
            expected = (90 if index == 0 else 24, 1 if index == 11 else 24,
                        3 if index == 0 else 5 if dw else 1,
                        2**((index-1)//2) if dw else 1, dw, index < 11 and not dw)
            actual = tuple(row[key] for key in ('inputs','outputs','kernel','dilation','depthwise','relu'))
            if actual != expected or len(row['shift']) != row['outputs']:
                raise ValueError('Integer graph layout differs')
            if any(not -31 <= shift <= 31 for shift in row['shift']):
                raise ValueError('Shift exceeds C11 representation')
            if any(not -128 <= weight <= 127 for weight in row['weights']):
                raise ValueError('Weight exceeds INT8 representation')
            if dw and any(row['bias']):
                raise ValueError('Exported depthwise bias must remain zero')
        self.metadata = deepcopy(model)
        self.channels = 24
        self.layers = nn.ModuleList(IntegerLayer(row, i) for i, row in enumerate(model['layers']))

    def features(self, value, active=None, *, trace=False):
        if value.ndim != 3 or value.shape[1] != 90 or not value.shape[0] or not value.shape[2]:
            raise ValueError('Expected nonempty batch x90 x frames')
        if value.dtype != torch.float32:
            raise ValueError('Exact-integer host convolution requires float32')
        raw = value.detach()
        if (not bool(torch.isfinite(raw).all()) or bool(((raw < -128) | (raw > 127)).any())
                or not torch.equal(raw, raw.round())):
            raise ValueError('Expected integer INT8 input values')
        if active is not None:
            if active.shape != (len(value), 1, value.shape[-1]) or active.dtype != torch.bool:
                raise ValueError('Expected boolean real-frame mask')
            value = value.masked_fill(~active, 0)
        traces = []
        for layer in self.layers:
            value = layer.quantize(layer.accumulate(value))
            if active is not None:
                value = value.masked_fill(~active, 0)
            if trace:
                traces.append(value)
        return (value, traces) if trace else value

    def export_model(self):
        result = deepcopy(self.metadata)
        for layer, row in zip(self.layers, result['layers']):
            weight = round_away(layer.weight.detach().clamp(-128, 127)).to(torch.int8)
            if not layer.depthwise:
                weight = weight.permute(0, 2, 1)
            row['weights'] = weight.contiguous().flatten().cpu().tolist()
        result.update(training_objective=CONTRACT, fixed_biases_and_shifts=True,
                      post_training_calibrations=0, trainable_integer_weights=9984,
                      auxiliary_head_exported=False)
        return result


def integer_context_forward(network, value, active, *, trace=False):
    if value.ndim != 3 or value.shape[1:] != (90, CONTEXT_FRAMES+256):
        raise ValueError('Expected the registered contextual90-input batch')
    output = network.features(value*32, active, trace=trace)
    if trace:
        score, layers = output
        return score[:, :, CONTEXT_FRAMES:]/256, [layer[:, :, CONTEXT_FRAMES:] for layer in layers]
    return output[:, :, CONTEXT_FRAMES:]/256


def checkpoint_integer(saved):
    config = saved.get('config', {})
    if (config.get('training_objective') != CONTRACT or config.get('topology') != TOPOLOGY
            or config.get('pitch_encoding') != ENCODING or config.get('inputs') != 90
            or config.get('channels') != 24 or config.get('post_training_calibrations') != 0):
        raise ValueError('Unregistered exact-integer training checkpoint')
    network = IntegerConditioned(saved['initial_integer_model'])
    state = saved.get('state_dict', {})
    if state.keys() != network.state_dict().keys():
        raise ValueError('Missing or unexpected integer checkpoint fields')
    for key, value in network.state_dict().items():
        if state[key].shape != value.shape or not torch.isfinite(state[key]).all():
            raise ValueError('Invalid integer checkpoint tensor: '+key)
        if key.endswith(('.bias', '.scale')) and not torch.equal(state[key], value):
            raise ValueError('Frozen integer bias/shift changed: '+key)
    return network
