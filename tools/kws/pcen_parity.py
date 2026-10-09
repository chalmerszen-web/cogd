"""Exact Python/C PCEN arithmetic and floating-formula error, no wake-quality claim."""
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import time

import numpy as np

ROOT = Path(__file__).resolve().parents[2]


class State(C.Structure):
    _fields_ = [('mean_q16', C.c_uint32 * 40)]


def main():
    out = ROOT / 'artifacts/kws-phase5/pcen-prototype'
    out.mkdir(exist_ok=False)
    library = ROOT / 'build-kws-pcen-prototype/libkws_pcen.so'
    lib = C.CDLL(str(library))
    lib.kws_pcen_reset.argtypes = [C.POINTER(State)]
    lib.kws_pcen_step.argtypes = [C.POINTER(State), C.POINTER(C.c_uint32), C.POINTER(C.c_int16)]
    state = State()
    lib.kws_pcen_reset(C.byref(state))
    assert C.sizeof(state) == 160
    rng = np.random.default_rng(20260929)
    data = rng.integers(0, 2 ** 32, (4096, 40), dtype=np.uint32)
    data[:5] = np.array([0, 2 ** 32 - 1, 0, 1, 4], dtype=np.uint32)[:, None]
    data[5:100] //= 2 ** 20
    estimate = [0] * 40
    output = (C.c_int16 * 40)()
    durations = []
    error = 0.
    traces = []
    for frame in data:
        expected = []
        for band, energy in enumerate(frame):
            magnitude = math.isqrt(int(energy))
            estimate[band] = (31 * estimate[band] + magnitude * 65536 + 16) // 32
            ratio = magnitude * 2 ** 28 // (estimate[band] + 65536)
            expected.append(math.isqrt((ratio + 8192) * 16) - math.isqrt(2 * 65536))
            floating = ((magnitude / (estimate[band] / 65536 + 1) + 2) ** .5 - math.sqrt(2)) * 256
            error = max(error, abs(floating - expected[-1]))
        start = time.perf_counter_ns()
        lib.kws_pcen_step(C.byref(state), frame.ctypes.data_as(C.POINTER(C.c_uint32)), output)
        durations.append((time.perf_counter_ns() - start) / 1000)
        assert list(output) == expected
        assert list(state.mean_q16) == estimate
        traces.append(expected)
    assert error < 1.01
    lib.kws_pcen_reset(C.byref(state))
    assert not any(state.mean_q16)
    np.savez_compressed(out / 'fixed-input-output.npz', power=data, expected=np.array(traces, dtype=np.int16))
    report = dict(passed=True, frames=4096, values=4096 * 40, state_bytes=160,
                  float_formula_max_error_q8=error, host_call_p99_us=float(np.percentile(durations, 99)),
                  host_call_max_us=max(durations), board_timing_verified=False, quality_verified=False,
                  library_sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
                  source_sha256=hashlib.sha256((ROOT / 'components/kws_c11/kws_pcen.c').read_bytes()).hexdigest(),
                  parameters=dict(alpha=1, delta=2, r=.5, epsilon_magnitude=1, smoothing='1/32'),
                  limitation='Standalone frontend prototype only. Current log-Mel model and firmware are unchanged; new normalization and trained weights required.')
    (out / 'parity.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    main()
