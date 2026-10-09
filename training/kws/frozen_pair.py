"""Host integer extraction from the original frozen24+24 backbones only."""
import numpy as np
import torch
from torch.nn import functional as F


class FrozenPair:
    def __init__(self, models):
        if len(models) != 2 or any(len(model['layers']) != 12
                                  or model['layers'][0]['outputs'] != 24 for model in models):
            raise ValueError('Two registered24 backbones required')
        self.branches = []
        for model in models:
            branch = []
            for index, row in enumerate(model['layers']):
                oc, ic, kernel = (row[name] for name in ('outputs', 'inputs', 'kernel'))
                raw = torch.tensor(row['weights'], dtype=torch.float64)
                weight = (raw.reshape(oc, 1, kernel) if row['depthwise'] else
                          raw.reshape(oc, kernel, ic).transpose(1, 2).contiguous())
                branch.append((row, weight, torch.tensor(row['bias'], dtype=torch.float64),
                               torch.pow(2., torch.tensor(row['shift'], dtype=torch.float64))[None, :, None],
                               index == 11))
            self.branches.append(branch)

    @torch.no_grad()
    def extract(self, features):
        x = np.asarray(features)
        if x.dtype != np.int8 or x.ndim != 3 or x.shape[-1] != 40 or min(x.shape) < 1:
            raise ValueError('Expected nonempty batch x time x40 INT8 features')
        hidden, scores = [], []
        for branch in self.branches:
            value = torch.as_tensor(x, dtype=torch.float64).transpose(1, 2)
            for index, (row, weight, bias, scale, final) in enumerate(branch):
                past = (row['kernel'] - 1) * row['dilation']
                value = F.conv1d(F.pad(value, (past, 0)), weight, bias,
                                 dilation=row['dilation'], groups=row['inputs'] if row['depthwise'] else 1)
                value = value.sign() * (value.abs() / scale + .5).floor()
                value = value.clamp(0 if row['relu'] else -32768 if final else -128,
                                    32767 if final else 127)
                if index == 10:
                    hidden.append(value.transpose(1, 2).numpy().astype(np.int8))
            scores.append(value[:, 0].numpy().astype(np.int16))
        return np.concatenate(hidden, axis=2), np.stack(scores, axis=2)
