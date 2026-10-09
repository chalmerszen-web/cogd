"""Full PCM/frontend regression and independent PCEN arithmetic check."""
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import sys
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from data import Frontend
from pcen_frontend import PcenFrontend
from parity import Backend


def main():
    path = ROOT / 'build-kws-pcen-prototype/libkws.so'
    rng = np.random.default_rng(20260930)
    signal = rng.integers(-32768, 32768, 512 * 256, dtype=np.int16)
    signal[:1024] = 0
    signal[1024:2048] = 32767
    old = Frontend()(signal)
    current = Frontend(path)(signal)
    np.testing.assert_array_equal(old, current)
    actual = PcenFrontend()(signal)
    backend = Backend(path)
    backend.lib.kws_frontend_power.argtypes = [C.c_void_p, C.POINTER(C.c_int16), C.POINTER(C.c_uint32)]
    power = (C.c_uint32 * 40)()
    mean = [0] * 40
    expected = []
    for frame in signal.reshape(-1, 256):
        backend.lib.kws_frontend_power(backend.handle, frame.ctypes.data_as(C.POINTER(C.c_int16)), power)
        values = []
        for band in range(40):
            magnitude = math.isqrt(power[band])
            mean[band] = (31 * mean[band] + magnitude * 65536 + 16) // 32
            ratio = magnitude * 2 ** 28 // (mean[band] + 65536)
            values.append(math.isqrt((ratio + 8192) * 16) - 362)
        expected.append(values)
    np.testing.assert_array_equal(actual, expected)
    repeated = PcenFrontend()
    np.testing.assert_array_equal(repeated(signal), repeated(signal))
    out = ROOT / 'artifacts/kws-phase5/pcen-prototype/full-frontend.json'
    with out.open('x') as f:
        json.dump(dict(passed=True, frames=512, logmel_unchanged=True, pcen_integer_exact=True,
                       library_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                       pcm_sha256=hashlib.sha256(signal.tobytes()).hexdigest(),
                       output_sha256=hashlib.sha256(actual.tobytes()).hexdigest()), f, indent=2)
    print(out.read_text())


if __name__ == '__main__':
    main()
