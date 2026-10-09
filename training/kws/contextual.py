"""Optional causal TRAIN windows with continuous frozen features and masked BN.

Only the training host uses this module. The exported24-channel C11 network
and its inference buffers do not change. Losses still receive256 local frames.
"""
import numpy as np
import torch
from torch.nn import functional as F

CONTEXT_FRAMES = 126


class ContextualInput:
    def __init__(self, raw, hidden, length, provenance, parents):
        if raw.dtype != np.int8 or raw.ndim != 3 or raw.shape[1:] != (256, 40):
            raise ValueError('Expected registered256x40 INT8 TRAIN inputs')
        if hidden.dtype != np.int8 or hidden.shape != (*raw.shape[:2], 48):
            raise ValueError('Frozen48 cache differs from TRAIN inputs')
        length = np.asarray(length)
        if length.shape != (len(raw),) or not np.issubdtype(length.dtype, np.integer):
            raise ValueError('Expected integer real-frame lengths')
        if np.any(length < 1) or np.any(length > 256):
            raise ValueError('Invalid real-frame lengths')
        self.raw, self.hidden, self.length = raw, hidden, length
        self.rows, self.parents = {}, parents
        for row in provenance:
            index, start, real = (row[name] for name in ('index', 'start_frame', 'real_frames'))
            if index in self.rows or not 0 <= index < len(raw) or start < 0 or start % 2:
                raise ValueError('Duplicate/out-of-range row or changed block phase')
            if real != int(length[index]):
                raise ValueError('Changed real-frame length')
            parent_raw, parent_hidden = parents[row['data_key'], row['source_index']]
            if parent_raw.dtype != np.int8 or parent_raw.ndim != 2 or parent_raw.shape[1] != 40:
                raise ValueError('Invalid parent features')
            if parent_hidden.dtype != np.int8 or parent_hidden.shape != (len(parent_raw), 48):
                raise ValueError('Invalid continuous parent representation')
            if start + real > len(parent_raw) or not np.array_equal(raw[index, :real], parent_raw[start:start+real]):
                raise ValueError('Crop does not match its unchanged parent')
            self.rows[index] = row

    def batch(self, indices):
        indices = np.asarray(indices)
        if indices.ndim != 1 or not len(indices) or not np.issubdtype(indices.dtype, np.integer):
            raise ValueError('Expected nonempty integer row indices')
        if np.any(indices < 0) or np.any(indices >= len(self.raw)):
            raise ValueError('Row index outside TRAIN')
        values = np.zeros((len(indices), CONTEXT_FRAMES+256, 88), dtype=np.int8)
        active = np.zeros((len(indices), 1, CONTEXT_FRAMES+256), dtype=bool)
        for output, index in enumerate(indices):
            real = int(self.length[index])
            row = self.rows.get(int(index))
            if row is None:
                values[output, CONTEXT_FRAMES:CONTEXT_FRAMES+real, :40] = self.raw[index, :real]
                values[output, CONTEXT_FRAMES:CONTEXT_FRAMES+real, 40:] = self.hidden[index, :real]
                active[output, 0, CONTEXT_FRAMES:CONTEXT_FRAMES+real] = True
            else:
                raw, hidden = self.parents[row['data_key'], row['source_index']]
                start = row['start_frame']
                past = min(CONTEXT_FRAMES, start)
                begin, end = CONTEXT_FRAMES-past, CONTEXT_FRAMES+real
                values[output, begin:end, :40] = raw[start-past:start+real]
                values[output, begin:end, 40:] = hidden[start-past:start+real]
                active[output, 0, begin:end] = True
        return values, active


def masked_batch_norm(module, value, active):
    """Exclude artificial pre-padding and invalid tails from BN statistics."""
    if not module.training:
        return module(value)
    if not module.track_running_stats:
        raise ValueError('Only the registered tracked BatchNorm is supported')
    if bool(active.all()):
        return module(value)
    count = active.sum().to(dtype=value.dtype)
    if int(count) < 2:
        raise ValueError('Masked BatchNorm requires at least two real positions')
    mean = value.masked_fill(~active, 0).sum((0, 2))/count
    centered = value-mean[None, :, None]
    variance = centered.square().masked_fill(~active, 0).sum((0, 2))/count
    with torch.no_grad():
        module.num_batches_tracked.add_(1)
        factor = module.momentum if module.momentum is not None else 1/float(module.num_batches_tracked)
        module.running_mean.lerp_(mean.detach(), factor)
        module.running_var.lerp_((variance*count/(count-1)).detach(), factor)
    output = centered*torch.rsqrt(variance[None, :, None]+module.eps)
    if module.affine:
        output = output*module.weight[None, :, None]+module.bias[None, :, None]
    return output


def contextual_forward(network, value, active, *, trace=False):
    return _contextual_forward(network, value, active, trace=trace, inputs=88)


def _contextual_forward(network, value, active, *, trace, inputs):
    if inputs not in (88, 90) or value.ndim != 3 or value.shape[1:] != (inputs, CONTEXT_FRAMES+256):
        raise ValueError('Expected registered contextual input dimensions')
    if active.shape != (value.shape[0], 1, value.shape[-1]) or active.dtype != torch.bool:
        raise ValueError('Expected matching boolean real-position mask')
    if value.shape[0] < 1 or network.channels != 24 or network.layers[0].conv.in_channels != inputs:
        raise ValueError('Only the registered conditioned24 model is supported')
    value = value.masked_fill(~active, 0)
    layers = []
    for index, layer in enumerate(network.layers):
        value = layer(value)
        if str(index) in network.norms:
            value = F.relu(masked_batch_norm(network.norms[str(index)], value, active))
        value = value.masked_fill(~active, 0)
        if trace:
            layers.append(value[:, :, CONTEXT_FRAMES:])
    output = value[:, :, CONTEXT_FRAMES:]
    return (output, layers) if trace else output
