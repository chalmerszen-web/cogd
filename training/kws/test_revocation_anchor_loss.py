"""Check the differentiable surrogate against actual C support transitions."""
import ctypes as C
import numpy as np
import torch
from anchor_loss import anchor_loss
from event_loss import event_opportunities
from revocation_anchor_loss import revocation_opportunities, revocation_anchor_loss


def verify(library):
    torch.set_num_threads(1)
    lib = C.CDLL(str(library))
    call = lib.support_flags
    call.argtypes = [C.POINTER(C.c_int16), C.c_uint, C.POINTER(C.c_uint8), C.c_uint]
    call.restype = C.c_uint
    checked = 0

    def compare(raw):
        nonlocal checked
        x = torch.from_numpy(raw.astype(np.float32))[None, None] / 256
        opportunity, ends = revocation_opportunities(x)
        ordinary, ordinary_ends = event_opportunities(x)
        assert torch.equal(ends, ordinary_ends) and torch.all(opportunity <= ordinary)
        # Independent integer rounding of the three raw odd-frame heads.
        heads = raw[1::2].astype(np.int64)
        sums = np.convolve(heads, np.ones(3, dtype=np.int64))[:len(heads)]
        divisor = np.minimum(np.arange(1, len(heads)+1), 3)
        smooth = np.where(sums >= 0, sums // divisor, -((-sums) // divisor)).astype(np.int16)
        flags = (C.c_uint8 * len(smooth))()
        assert call(smooth.ctypes.data_as(C.POINTER(C.c_int16)), len(smooth), flags, len(flags)) == len(smooth)
        actual = np.array(flags, dtype=bool)[2:]
        expected = ((opportunity[0]*256 >= 268) & (ends >= 32768)).numpy()
        np.testing.assert_array_equal(actual, expected)
        checked += len(actual)
        return x, opportunity, ends

    rng = np.random.default_rng(20261002311)
    for i in range(64):
        raw = rng.integers(-1800,2201,512,dtype=np.int16)
        if i < 4:
            raw.fill(1000)
            if i: raw[127:127+2*i] = -1000
        compare(raw)

    # The observed loss has smoothed scores [1132,901,-72] at its legal anchor.
    raw = np.zeros(256, dtype=np.int16)
    raw[2*97+1], raw[2*98+1], raw[2*99+1] = 3396, -693, -2919
    x, opportunity, end = compare(raw)
    anchors = torch.zeros(2, 126, dtype=torch.bool)
    anchors[0, 97] = True
    values = x.repeat(2, 1, 1).detach().requires_grad_(True)
    ordinary, _ = anchor_loss(values, anchors)
    revised, report = revocation_anchor_loss(values, anchors)
    assert ordinary == 0 and revised > 0 and report['anchors'] == 1
    assert opportunity[0, 97]*256 == 196 and end[97] == 51200
    revised.backward()
    assert values.grad[0, 0, 199] < 0
    assert not values.grad[1].any() and not values.grad[0, 0, 200:].any()
    # Changing later data cannot alter an earlier anchored value.
    changed = values.detach().clone()
    changed[:, :, 200:] = 10000
    a, _ = revocation_anchor_loss(values, anchors)
    b, _ = revocation_anchor_loss(changed, anchors)
    torch.testing.assert_close(a, b, rtol=0, atol=0)
    empty = torch.zeros_like(anchors)
    value, _ = revocation_anchor_loss(values, empty)
    assert value == 0 and torch.isfinite(value)
    for mask, margin in ((anchors.float(), 1), (anchors[:, :-1], 1), (anchors, 0)):
        try: revocation_anchor_loss(values, mask, margin)
        except ValueError: pass
        else: raise AssertionError('Invalid anchor metadata accepted')
    return dict(passed=True, random_streams=64, C_support_blocks=checked,
        ordinary_loss_zero_revised_positive=True, observed_anchor_proxy_q8=196,
        raises_current_negative_gradient=True, no_future_or_unanchored_gradient=True,
        causal_future_change_invariant=True, empty_mask_zero=True,
        invalid_metadata_rejected=True, surrogate_not_above_median=True,
        newly_recorded_support_not_full_owner_policy=True)


if __name__ == '__main__':
    import argparse, json
    from pathlib import Path
    parser = argparse.ArgumentParser()
    parser.add_argument('--library', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.library)))
