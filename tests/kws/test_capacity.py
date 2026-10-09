"""A wider host diagnostic must keep causal streaming and the default topology."""
import sys
from pathlib import Path

import pytest
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'training/kws'))
from model import Model


def test_host_widths_stream_and_preserve_default():
    torch.set_num_threads(1)
    torch.manual_seed(13)
    assert sum(p.numel() for p in Model().parameters()) == 6673
    for channels in (24, 48):
        net = Model(channels).eval()
        assert sum(p.numel() for p in net.parameters()) == 5 * channels ** 2 + 158 * channels + 1
        signal = torch.randn(2, 40, 150)
        with torch.no_grad():
            expected = net(signal)
            states, parts = None, []
            for piece in signal.split(7, dim=-1):
                result, states = net.step(piece, states)
                parts.append(result)
        torch.testing.assert_close(torch.cat(parts, dim=-1), expected, rtol=1e-5, atol=1e-7)
    with pytest.raises(ValueError):
        Model(96)
