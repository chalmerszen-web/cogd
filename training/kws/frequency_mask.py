"""Optional host-only narrow frequency masking, with an unchanged time axis.

This is a bounded SpecAugment-inspired experiment, not the default trainer.
Zero is the training mean in the existing normalized feature coordinates;
it does not represent microphone silence. No time masking or time warping.
"""
import numpy as np
import torch


def frequency_mask(features, rng, *, maximum=4, probability=.5):
    """Copy Bx40xT floating features and mask at most four adjacent bands.

    Return (copy, starts, effective_widths). The caller owns an independent
    NumPy Generator so augmentation never consumes the sampler/model RNG.
    """
    if (not isinstance(features, torch.Tensor) or features.ndim != 3
            or features.shape[0] < 1 or features.shape[1] != 40
            or features.shape[2] < 1 or not features.is_floating_point()):
        raise ValueError('Expected nonempty Bx40xT floating features')
    if not isinstance(rng, np.random.Generator):
        raise ValueError('Dedicated NumPy Generator required')
    if type(maximum) is not int or not 0 <= maximum <= 4:
        raise ValueError('At most four frequency bands can be masked')
    if not np.isfinite(probability) or not 0 <= probability <= 1:
        raise ValueError('Mask probability must be in [0,1]')
    count = features.shape[0]
    widths = rng.integers(0, maximum + 1, size=count, dtype=np.int64)
    starts = rng.integers(0, 41 - widths, size=count, dtype=np.int64)
    widths = np.where(rng.random(count) < probability, widths, 0)
    output = features.clone()
    for index in np.flatnonzero(widths):
        start, width = int(starts[index]), int(widths[index])
        output[int(index), start:start + width, :] = 0
    return output, starts, widths
