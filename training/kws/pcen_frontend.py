"""Exact experimental C frontend for training; never applies PCEN to old weights."""
import ctypes as C
import numpy as np
from data import ROOT, Frontend
from parity import Trace


class State(C.Structure):
    _fields_ = [('mean_q16', C.c_uint32 * 40)]


class PcenFrontend(Frontend):
    def __init__(self, library=ROOT / 'build-kws-pcen-prototype/libkws.so'):
        super().__init__(library)
        self.state = State()
        self.backend.lib.kws_pcen_reset.argtypes = [C.POINTER(State)]
        self.backend.lib.kws_pcen_frontend.argtypes = [C.c_void_p, C.POINTER(State), C.POINTER(C.c_int16), C.POINTER(Trace)]

    def __call__(self, pcm):
        backend = self.backend
        backend.lib.kws_reset(backend.handle)
        backend.lib.kws_pcen_reset(C.byref(self.state))
        padded = np.pad(np.asarray(pcm, dtype=np.int16), (0, (-len(pcm)) % 256))
        result = np.empty((len(padded) // 256, 40), dtype=np.int16)
        trace = Trace()
        for index, frame in enumerate(padded.reshape(-1, 256)):
            backend.lib.kws_pcen_frontend(backend.handle, C.byref(self.state), frame.ctypes.data_as(C.POINTER(C.c_int16)), C.byref(trace))
            result[index] = trace.logmel
        return result
