"""Train affine acoustic layers without changing the C11 inference ABI.

Bias parameters use output units so their gradient scale is independent of
the frozen power-of-two requantization shift. Export keeps INT32 biases;
they must never be projected into the INT8 weight range.
"""
import torch
from torch import nn
from torch.nn import functional as F
from full_pair_qat import Trainable, FullTemporalPairedQAT
from pair_qat import round_away, straight_integer


ACCUMULATOR_LIMIT = (1 << 23)-1


class Affine(Trainable):
    def __init__(self, metadata, index):
        super().__init__(metadata, index)
        self.product_margin = self.maximum_partial_sum-max(abs(v) for v in metadata['bias'])
        self.bias_limit = ACCUMULATOR_LIMIT-self.product_margin
        if self.bias_limit <= 0 or max(abs(v) for v in metadata['bias']) > self.bias_limit:
            raise ValueError('Bias has no exact-FP32 accumulator headroom')
        units = self.bias/self.scale.flatten()
        del self.bias
        self.bias_units = nn.Parameter(units)
        self.maximum_partial_sum = ACCUMULATOR_LIMIT

    def integer_bias(self):
        raw = (self.bias_units*self.scale.flatten()).clamp(-self.bias_limit, self.bias_limit)
        return straight_integer(raw, round_away(raw))

    def accumulate(self, values):
        clamped = self.weight.clamp(-128, 127)
        weight = straight_integer(clamped, round_away(clamped))
        return F.conv1d(F.pad(values, (self.dilation*(self.kernel-1), 0)),
            weight, self.integer_bias(), dilation=self.dilation,
            groups=self.inputs if self.depthwise else 1)


class AffineTemporalPairedQAT(FullTemporalPairedQAT):
    def __init__(self, models):
        super().__init__(models)
        self.branches = nn.ModuleList([nn.ModuleList([Affine(layer, i)
            for i, layer in enumerate(model['layers'])]) for model in models])

    def export_models(self):
        models = super().export_models()
        for layers, model in zip(self.branches, models):
            for layer, metadata in zip(layers, model['layers']):
                metadata['bias'] = layer.integer_bias().detach().to(torch.int32).cpu().tolist()
                bound = layer.product_margin+max(abs(v) for v in metadata['bias'])
                if bound > ACCUMULATOR_LIMIT:
                    raise ValueError('Export exceeds proven partial-sum bound')
        return models

    @torch.no_grad()
    def project_parameters(self):
        self.cross.clamp_(-128, 127)
        self.temporal.clamp_(-128, 127)
        for layers in self.branches:
            for layer in layers:
                layer.weight.clamp_(-128, 127)
                limit = layer.bias_limit/layer.scale.flatten()
                layer.bias_units.copy_(torch.maximum(-limit, torch.minimum(limit, layer.bias_units)))
