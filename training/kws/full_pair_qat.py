"""Train acoustic INT8 weights in the existing bounded C11 paired topology.

Biases, shifts, normalization, topology and causal workspace remain fixed.
The host uses STE only for gradients; every forward value is an integer.
"""
from copy import deepcopy
import torch
from torch import nn
from torch.nn import functional as F
from pair_qat import Fixed, round_away, straight_integer
from temporal_pair_qat import TemporalPairedQAT, TEMPORAL


class Trainable(Fixed):
    def __init__(self, metadata, index):
        super().__init__(metadata, index)
        weight = self.weight.clone()
        del self.weight
        self.weight = nn.Parameter(weight)
        if index in TEMPORAL:
            self.maximum_partial_sum += 5*16384
        if self.maximum_partial_sum >= 2**24:
            raise ValueError('Float32 exact-integer bound exceeded')

    def accumulate(self, values):
        clamped = self.weight.clamp(-128, 127)
        weight = straight_integer(clamped, round_away(clamped))
        return F.conv1d(F.pad(values, (self.dilation*(self.kernel-1), 0)),
            weight, self.bias, dilation=self.dilation, groups=self.inputs if self.depthwise else 1)


class FullTemporalPairedQAT(TemporalPairedQAT):
    def __init__(self, models):
        super().__init__(models)
        self.branches = nn.ModuleList([nn.ModuleList([Trainable(layer, index)
            for index, layer in enumerate(model['layers'])]) for model in models])

    def export_models(self):
        models = deepcopy(self.metadata)
        for layers, model in zip(self.branches, models):
            for layer, metadata in zip(layers, model['layers']):
                weight = round_away(layer.weight.detach().clamp(-128, 127)).to(torch.int8)
                if not layer.depthwise:
                    weight = weight.permute(0, 2, 1)
                metadata['weights'] = weight.contiguous().flatten().cpu().tolist()
        return models
