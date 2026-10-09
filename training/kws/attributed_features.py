"""Recover a frozen48 model's features from verified C3 input, using native C.

The caller must verify source attribution and library/source hashes first.
This helper does not assign labels, change dataset splits, or train a model.
"""
import ctypes as C
import zlib

import numpy as np


class Trace48(C.Structure):
    _fields_ = [('logmel', C.c_int16 * 40), ('input', C.c_int8 * 40),
                ('layer', C.c_int16 * 529)]


def extract(library, raw, rows, *, symbol='kws_trained_model', prime_samples=32768):
    """Return INT8 frontend/penultimate features and Q8 heads for every16ms.

    The exact256-sample frontend and all48 channels run in the existing C
    library. Each captured512-sample block must have contiguous sequence,
    original sample clock, CRC, all three heads, and no inference error.
    Every second recovered head must equal the actual device verifier head.
    """
    if not rows or len(raw) != len(rows) * 1024 or prime_samples != 32768:
        raise ValueError('Expected complete512-sample input and original prime')
    lib = C.CDLL(str(library))
    if lib.kws_channels() != 48 or lib.kws_trace_values() != 529:
        raise ValueError('Expected the registered48 C trace ABI')
    lib.kws_workspace_size.restype = C.c_size_t
    lib.kws_workspace_alignment.restype = C.c_size_t
    lib.kws_init.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p,
                            C.POINTER(C.c_void_p)]
    lib.kws_step_pcm.argtypes = [C.c_void_p, C.POINTER(C.c_int16),
                                C.POINTER(Trace48)]
    memory = C.create_string_buffer(lib.kws_workspace_size())
    if C.addressof(memory) % lib.kws_workspace_alignment():
        raise ValueError('Unaligned C workspace')
    handle = C.c_void_p()
    model = C.c_char.in_dll(lib, symbol)
    if lib.kws_init(memory, len(memory), C.byref(model), C.byref(handle)):
        raise ValueError('Native C model rejected')
    trace, zeros = Trace48(), (C.c_int16 * 256)()
    for _ in range(prime_samples // 256):
        lib.kws_step_pcm(handle, zeros, C.byref(trace))
    features = np.empty((2 * len(rows), 40), dtype=np.int8)
    hidden = np.empty((2 * len(rows), 48), dtype=np.int8)
    heads = np.empty(2 * len(rows), dtype=np.int16)
    for i, row in enumerate(rows):
        block = raw[i * 1024:(i + 1) * 1024]
        if (row['sequence'] != i or row['sample_end'] != prime_samples + (i + 1) * 512
                or row['crc'] != zlib.crc32(block) or row['flags'] & 4
                or not row['flags'] & 16 or row['armed'] != bool(row['flags'] & 8)):
            raise ValueError(f'Invalid captured block{i}')
        for half in range(2):
            pcm = (C.c_int16 * 256).from_buffer_copy(block[half * 512:(half + 1) * 512])
            lib.kws_step_pcm(handle, pcm, C.byref(trace))
            values = list(trace.layer[480:528])
            if min(values) < 0 or max(values) > 127:
                raise ValueError('Penultimate ReLU exceeds registered INT8 range')
            j = 2 * i + half
            features[j], hidden[j], heads[j] = list(trace.input), values, trace.layer[528]
        if int(heads[2 * i + 1]) != row['heads_q8'][2]:
            raise ValueError(f'C3 verifier head mismatch at block{i}')
    return dict(x=features, hidden=hidden, heads=heads)
