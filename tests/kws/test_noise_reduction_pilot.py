import sys
from pathlib import Path
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'training/kws'))
from noise_reduction_pilot import reduce


def test_causal_chunks_and_no_lookahead():
    x = np.random.default_rng(17).integers(0, 7000, (190, 40), dtype=np.int16)
    complete, _ = reduce(x)
    state = None
    pieces = []
    for segment in (x[:1], x[1:37], x[37:100], x[100:]):
        output, state = reduce(segment, state)
        pieces.append(output)
    np.testing.assert_array_equal(np.concatenate(pieces), complete)
    prefix, _ = reduce(x[:80])
    np.testing.assert_array_equal(prefix, complete[:80])


def test_floor_preserves_stationary_energy_and_silence():
    silence, _ = reduce(np.zeros((100, 40), dtype=np.int16))
    assert not silence.any()
    steady, _ = reduce(np.full((500, 40), 4096, dtype=np.int16))
    assert np.all(steady[-1] == 4096 - 512)
    assert np.all((steady >= 3584) & (steady <= 4096))
