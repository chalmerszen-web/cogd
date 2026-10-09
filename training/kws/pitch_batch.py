"""Fast host batching of existing C frontend and the same C3 pitch feature."""
import ctypes as C
from pathlib import Path
import numpy as np
from data import ROOT
from parity import Backend


class BatchFrontend:
    def __init__(self,library,frontend_library=ROOT/'build-kws-host/libkws.so'):
        self.backend=Backend(Path(frontend_library))
        self.lib=C.CDLL(str(Path(library).resolve()))
        self.lib.kws_pitch_frontend_batch.argtypes=[C.c_void_p,C.POINTER(C.c_int16),C.c_size_t,
            C.POINTER(C.c_int16),C.POINTER(C.c_int16)]
        self.lib.kws_pitch_frontend_batch.restype=C.c_int

    def __call__(self,pcm):
        if not isinstance(pcm,np.ndarray) or pcm.ndim!=1 or pcm.dtype!=np.dtype('int16'):
            raise ValueError('Expected native-endian PCM16 mono16kHz')
        if not len(pcm) or len(pcm)%256 or len(pcm)>65536*256:
            raise ValueError('Expected1..65536 complete256-sample blocks')
        pcm=np.ascontiguousarray(pcm)
        logmel=np.empty((len(pcm)//256,40),dtype=np.int16)
        pitch=np.empty((len(pcm)//256,2),dtype=np.int16)
        pointer=lambda x:x.ctypes.data_as(C.POINTER(C.c_int16))
        if self.lib.kws_pitch_frontend_batch(self.backend.handle,pointer(pcm),len(logmel),pointer(logmel),pointer(pitch)):
            raise RuntimeError('C batch rejected arguments')
        return logmel,pitch
