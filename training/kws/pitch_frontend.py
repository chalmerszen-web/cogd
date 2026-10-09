"""Training wrapper for the same stateful C11 pitch feature used by C3.

Raw output columns are HzQ4 and periodicityQ12. This does not normalize,
classify, pad, resample or select a split. Caller supplies PCM16 at 16kHz.
"""
import ctypes as C
from pathlib import Path
import numpy as np


class State(C.Structure):
    _fields_ = [('window', C.c_int16*384), ('seen', C.c_uint16)]


class Feature(C.Structure):
    _fields_ = [('frequency_q4', C.c_int16), ('periodicity_q12', C.c_int16)]


class Pitch:
    def __init__(self, library):
        if C.sizeof(State) != 770 or C.sizeof(Feature) != 4:
            raise RuntimeError('Unsupported pitch ABI')
        self.lib = C.CDLL(str(Path(library).resolve()))
        self.lib.kws_pitch_reset.argtypes = [C.POINTER(State)]
        self.lib.kws_pitch_reset.restype = None
        self.lib.kws_pitch_block.argtypes = [C.POINTER(State), C.POINTER(C.c_int16)]
        self.lib.kws_pitch_block.restype = Feature
        self.state = State()
        self.reset()

    def reset(self):
        self.lib.kws_pitch_reset(C.byref(self.state))

    def __call__(self, pcm, *, reset=True):
        """Return one feature per complete256-sample block, with causal state.

        reset=False continues a preceding chunk; empty input never advances
        state. Incomplete blocks are rejected rather than silently padded.
        """
        if not isinstance(pcm, np.ndarray) or pcm.ndim != 1 or pcm.dtype != np.dtype('int16'):
            raise ValueError('Expected a native-endian one-dimensional PCM16 array')
        if len(pcm) % 256:
            raise ValueError('PCM must contain complete256-sample blocks')
        if reset:
            self.reset()
        pcm = np.ascontiguousarray(pcm)
        result = np.empty((len(pcm)//256, 2), dtype=np.int16)
        for i, frame in enumerate(pcm.reshape(-1, 256)):
            feature = self.lib.kws_pitch_block(C.byref(self.state), frame.ctypes.data_as(C.POINTER(C.c_int16)))
            result[i] = feature.frequency_q4, feature.periodicity_q12
        return result
