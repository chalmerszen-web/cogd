"""Numeric and causal tests of the actual detector-aligned training loss."""
import ctypes as C
from pathlib import Path
import sys
import numpy as np
import torch
from event_loss import decision_scores, event_opportunities, event_loss
from pair_qat import events

ROOT = Path(__file__).resolve().parents[2]


def smooth(raw):
    blocks = raw[1::2].astype(np.int32)
    return np.asarray([int(sum(map(int, blocks[max(0, i - 2):i + 1])) / min(3, i + 1))
                       for i in range(len(blocks))], dtype=np.int16)


def main():
    torch.set_num_threads(4)
    rng = np.random.default_rng(2026100267)
    raw = rng.integers(-32768, 32768, (8, 1024), dtype=np.int16)
    raw[0].fill(-32768); raw[1].fill(32767)
    logits = torch.tensor(raw[:, None], dtype=torch.float32) / 256
    scores = decision_scores(logits).numpy()
    for i in range(8):
        np.testing.assert_array_equal(scores[i], smooth(raw[i]))
    opportunities, end = event_opportunities(logits)
    for i in range(8):
        possible = end[(opportunities[i] * 256 >= 268) & (end >= 32768)].tolist()
        actual = events(scores[i])
        assert (possible[0] if possible else None) == (actual[0] if actual else None)
        for n in (64, 127, 128, 129, 511, 513, 1023):
            np.testing.assert_array_equal(decision_scores(logits[i:i + 1, :, :n]).numpy()[0], scores[i, :n // 2])
    path = ROOT / 'artifacts/voice-fast/wake-wide-train-ux264/libcandidate.so'
    lib = C.CDLL(str(path))
    fun = lib.wide_events
    fun.argtypes = [C.POINTER(C.c_int16), C.c_uint, C.POINTER(C.c_uint64), C.c_uint]
    fun.restype = C.c_uint
    for r in raw:
        output = (C.c_uint64 * 1024)()
        n = fun(r.ctypes.data_as(C.POINTER(C.c_int16)), len(r), output, len(output))
        assert list(output)[:n] == events(smooth(r))
    for positive in (False, True):
        values = torch.full((2, 1, 256), -2., requires_grad=True)
        target = torch.zeros_like(values)
        loss, row = event_loss(values, target, torch.tensor([positive] * 2),
            torch.tensor([40000] * 2), torch.tensor([50000] * 2), torch.tensor([256] * 2))
        loss.backward()
        assert torch.isfinite(loss) and torch.isfinite(values.grad).all()
        assert row['positive_windows'] == (2 if positive else 0)
        # The C vote/mean retains pre-warm history; earliest supporting head119.
        assert not torch.any(values.grad[:, :, :119])
        if positive:
            assert torch.any(values.grad < 0)
    # A padded high tail and a wholly invalid/short parent must not affect loss.
    lengths = torch.tensor([160, 80])
    good = torch.full((2, 1, 256), -3.)
    bad = good.clone(); bad[0, :, 160:] = 100.; bad[1].fill_(100.)
    args = (torch.zeros_like(good), torch.tensor([False, False]),
            torch.tensor([-1, -1]), torch.tensor([-1, -1]), lengths)
    a, _ = event_loss(good, *args); b, _ = event_loss(bad, *args)
    torch.testing.assert_close(a, b, rtol=0, atol=0)
    # Positive parent without local accepted endpoints becomes negative.
    _, row = event_loss(good, torch.zeros_like(good), torch.tensor([True, True]),
        torch.tensor([90000, 90000]), torch.tensor([100000, 100000]), torch.tensor([256, 256]))
    assert row['positive_windows'] == 0
    try:
        decision_scores(torch.zeros(1, 40, 256))
    except ValueError:
        pass
    else:
        raise AssertionError('Wrong topology accepted')
    print('PASS:4096 exact scores/C events, causal prefixes, gradients, warm/tail masks, invalid local positives, topology')


if __name__ == '__main__':
    main()
