"""Conservative causal magnitude subtraction for offline compatibility diagnosis.

Uses a rolling per-band estimate as in TensorFlow microfrontend's documented
noise-reduction approach; deliberately different fixed conservative constants.
No model changes and no claim of PCEN or deployable integer equivalence.
"""
import numpy as np


def reduce(logmel, state=None):
    values = np.asarray(logmel)
    if values.ndim != 2 or values.shape[1] != 40:
        raise ValueError('Expected frames by forty Q8 log-energy bands')
    estimate = np.zeros(40) if state is None else np.asarray(state, dtype=float).copy()
    result = np.empty(values.shape, dtype=np.int16)
    for index, frame in enumerate(values):
        magnitude = np.exp2(frame.astype(float) / 512)
        estimate += (magnitude - estimate) / 32
        cleaned = np.maximum(magnitude - estimate, magnitude * .5)
        result[index] = np.clip(np.rint(512 * np.log2(np.maximum(cleaned, 1))), 0, 32767)
    return result, estimate
