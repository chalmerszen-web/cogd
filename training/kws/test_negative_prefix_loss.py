"""Finite semantic/gradient checks; no dataset or model selection."""
import torch
from event_loss import event_opportunities, THRESHOLD_Q8
from negative_prefix_loss import negative_prefix_loss


def verify():
    torch.set_num_threads(1)
    torch.manual_seed(20261002306)
    negative = torch.tensor([True, False])
    length = torch.tensor([256, 256])
    x = torch.full((2, 1, 256), -4., requires_grad=True)
    # A forbidden near-word peak well beyond the first eight blocks.
    with torch.no_grad():
        x[:, :, 38:52] = 6.
    loss, report = negative_prefix_loss(x, negative, length)
    assert report['prefix_forbidden'] > 0 and report['prefix_negative_windows'] == 1
    loss.backward()
    assert x.grad[0, 0, 38:52].sum() > 0
    assert not x.grad[1].any() and not x.grad[:, :, 127:].any()
    # No labels -> exactly zero, finite, differentiable loss.
    loss, report = negative_prefix_loss(x, torch.zeros(2, dtype=torch.bool), length)
    assert loss == 0 and report['prefix_negative_windows'] == 0
    short = torch.randn(2, 1, 256, requires_grad=True)
    small = torch.tensor([12, 0])
    loss, _ = negative_prefix_loss(short, torch.ones(2, dtype=torch.bool), small)
    loss.backward()
    assert not short.grad[0, :, 12:].any() and not short.grad[1].any()
    # Padding and post-warm changes do not alter this term.
    with torch.no_grad():
        changed = short.detach().clone()
        changed[0, :, 12:] = 10000.
        changed[1] = -10000.
    a, _ = negative_prefix_loss(short, torch.ones(2, dtype=torch.bool), small)
    b, _ = negative_prefix_loss(changed, torch.ones(2, dtype=torch.bool), small)
    torch.testing.assert_close(a, b, rtol=0, atol=0)
    # Independent opportunity mask/hinge over randomized lengths and labels.
    for _ in range(64):
        z = torch.randn(7, 1, 256) * 4
        labels = torch.rand(7) < .5
        real = torch.randint(0, 257, (7,))
        value, report = negative_prefix_loss(z, labels, real)
        opportunities, ends = event_opportunities(z)
        penalties = []
        point_values = []
        for row in range(7):
            if not labels[row]:
                continue
            eligible = [j for j, end in enumerate(ends) if end < 32768 and end <= real[row] * 256]
            if eligible:
                worst = max(float(opportunities[row, j]) for j in eligible)
                penalties.append(max(0., worst - (THRESHOLD_Q8 / 256 - 1.)) ** 2)
            for j in range(min(127, int(real[row]))):
                point_values.append(torch.nn.functional.softplus(z[row, 0, j]))
        expected = (sum(penalties) / max(1, len(penalties)) +
            .25 * (torch.stack(point_values).mean().item() if point_values else 0.))
        assert abs(float(value) - expected) < 2e-5
    for labels, real, margin in ((negative.int(), length, 1),
        (negative, torch.tensor([257, 256]), 1), (negative, length, 0)):
        try:
            negative_prefix_loss(x, labels, real, margin)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid metadata accepted')
    return dict(passed=True, random_cases=64, positive_parent_excluded=True,
        prefix_peak_gradient=True, no_postwarm_gradient=True, padded_tail_ignored=True,
        empty_mask_zero=True, invalid_metadata_rejected=True)


if __name__ == '__main__':
    import json
    print(json.dumps(verify()))
