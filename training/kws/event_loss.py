"""Host training loss aligned with the bounded C11 two-of-three detector."""
import torch
from torch.nn import functional as F

THRESHOLD_Q8 = 268


def _straight(value, rounded):
    return value + (rounded - value).detach()


def decision_scores(logits):
    """Exact signed Q8/three-block forward values; straight-through gradients."""
    if logits.ndim != 3 or logits.shape[1] != 1 or logits.shape[-1] < 6:
        raise ValueError('Expected batch x1 x at least6 feature frames')
    scaled = logits[:, 0] * 256
    head = _straight(scaled, scaled.sign() * (scaled.abs() + .5).floor()).clamp(-32768, 32767)
    block = head[:, 1::2]
    sums = F.conv1d(F.pad(block[:, None], (2, 0)), block.new_ones(1, 1, 3))[:, 0]
    divisor = torch.arange(1, block.shape[1] + 1, device=block.device).clamp_max(3)
    mean = sums / divisor
    return _straight(mean, mean.trunc())


def event_opportunities(logits):
    """A window median exceeds268 exactly when two of its three scores do."""
    scores = decision_scores(logits)
    opportunity = scores.unfold(1, 3, 1).median(-1).values / 256
    end = (torch.arange(opportunity.shape[1], device=logits.device) + 3) * 512
    return opportunity, end


def event_loss(logits, target, positive, low, high, length, margin=1.):
    """Train accepted peaks and every forbidden opportunity, before cooldown.

    low/high are local sample endpoints; high already includes acceptance
    tolerance. A positive parent window with no accepted post-warm endpoint
    is treated as a negative window. Invalid padded tails never contribute.
    Frame BCE ignores labels that conflict with the accepted event interval.
    """
    batch, _, frames = logits.shape
    if target.shape != logits.shape or any(value.shape != (batch,) for value in (positive, low, high, length)):
        raise ValueError('Mismatched event metadata')
    if margin <= 0:
        raise ValueError('Positive margin required')
    opportunity, end = event_opportunities(logits)
    valid = (end[None] >= 32768) & (end[None] <= length[:, None] * 256)
    accepted = valid & positive[:, None] & (end[None] >= low[:, None]) & (end[None] <= high[:, None])
    forbidden = valid & ~accepted
    positive_rows, negative_rows = accepted.any(1), forbidden.any(1)
    threshold = THRESHOLD_Q8 / 256
    best = opportunity.masked_fill(~accepted, -65536).max(1).values
    worst = opportunity.masked_fill(~forbidden, -65536).max(1).values
    positive_penalty = (F.relu(threshold + margin - best).square() * positive_rows).sum() / positive_rows.sum().clamp_min(1)
    negative_penalty = (F.relu(worst - (threshold - margin)).square() * negative_rows).sum() / negative_rows.sum().clamp_min(1)
    frame_end = (torch.arange(frames, device=logits.device) + 1) * 256
    frame_valid = (frame_end[None] >= 32768) & (frame_end[None] <= length[:, None] * 256) & (target[:, 0] >= 0)
    frame_accepted = positive[:, None] & (frame_end[None] >= low[:, None]) & (frame_end[None] <= high[:, None])
    aligned = frame_valid & ((target[:, 0] > 0) == frame_accepted)
    frame = F.binary_cross_entropy_with_logits(logits[:, 0], target[:, 0].clamp_min(0), reduction='none')
    point_penalty = (frame * aligned).sum() / aligned.sum().clamp_min(1)
    loss = positive_penalty + negative_penalty + .25 * point_penalty
    return loss, dict(positive=positive_penalty.detach(), forbidden=negative_penalty.detach(),
        point=point_penalty.detach(), positive_windows=positive_rows.sum().detach(),
        forbidden_windows=negative_rows.sum().detach())
