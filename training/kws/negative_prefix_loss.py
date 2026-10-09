"""Supervise labelled negatives even before a cold window can accept wakes.

This is a training term, never a change to the runtime warm-up/threshold.
Positive parents are excluded so a correctly completed pre-warm word is not
mislabelled merely because the runtime was not yet armed in that window.
"""
import torch
from torch.nn import functional as F
from event_loss import event_opportunities, THRESHOLD_Q8


def negative_prefix_loss(logits, negative, length, margin=1.):
    if logits.ndim != 3 or logits.shape[1] != 1 or logits.shape[-1] < 6:
        raise ValueError('Expected batch x1 x at least six feature frames')
    batch, _, frames = logits.shape
    if negative.shape != (batch,) or negative.dtype != torch.bool or length.shape != (batch,):
        raise ValueError('Expected boolean negative labels and real frame lengths')
    if margin <= 0 or torch.any(length < 0) or torch.any(length > frames):
        raise ValueError('Invalid margin or real frame length')
    opportunity, end = event_opportunities(logits)
    mask = negative[:, None] & (end[None] < 32768) & (end[None] <= length[:, None] * 256)
    rows = mask.any(1)
    worst = opportunity.masked_fill(~mask, -65536).max(1).values
    hinge = F.relu(worst - (THRESHOLD_Q8 / 256 - margin)).square()
    events = (hinge * rows).sum() / rows.sum().clamp_min(1)
    frame_end = (torch.arange(frames, device=logits.device) + 1) * 256
    frame_mask = negative[:, None] & (frame_end[None] < 32768) & (frame_end[None] <= length[:, None] * 256)
    points = F.binary_cross_entropy_with_logits(logits[:, 0], torch.zeros_like(logits[:, 0]), reduction='none')
    point = (points * frame_mask).sum() / frame_mask.sum().clamp_min(1)
    return events + .25 * point, dict(prefix_forbidden=events.detach(),
        prefix_point=point.detach(), prefix_negative_windows=rows.sum().detach())
