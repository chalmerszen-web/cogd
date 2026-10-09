"""Preserve correctly annotated original wake endpoints during host training."""
import numpy as np
import torch
from torch.nn import functional as F
from event_loss import event_opportunities, THRESHOLD_Q8


def accepted_anchor_mask(event_lists, label, low, high, length, frames=256):
    """Only annotated positives can provide anchors; original false hits cannot.

    All coordinates are local samples. high includes the existing tolerance.
    A translated parent event outside this window or its warm-up contributes
    nothing. The mask uses exactly the event_opportunities endpoint geometry.
    """
    label, low, high, length = (np.asarray(value) for value in (label, low, high, length))
    count = len(event_lists)
    if type(frames) is not int or frames < 6 or frames % 2:
        raise ValueError('Even feature window of at least six frames required')
    if any(value.shape != (count,) for value in (label, low, high, length)):
        raise ValueError('Anchor metadata shape differs')
    if not np.isin(label, [0, 1]).all() or (length < 0).any() or (length > frames).any():
        raise ValueError('Invalid label or real window length')
    result = np.zeros((count, frames // 2 - 2), dtype=bool)
    for i, anchors in enumerate(event_lists):
        last = None
        for end in anchors:
            if not isinstance(end, (int, np.integer)) or end % 512 or (last is not None and end <= last):
                raise ValueError('Anchors must be increasing integer block endpoints')
            last = end
            if label[i] and 32768 <= end <= int(length[i]) * 256 and low[i] <= end <= high[i]:
                result[i, int(end) // 512 - 3] = True
    return result


def anchor_loss(logits, anchors, margin=1.):
    """Hinge on every original legal accepted endpoint, before new cooldown.

    This adds temporal supervision to event_loss; it does not replace its
    negative/early penalties. Forward Q8/mean/twoof3 geometry is unchanged.
    """
    opportunity, _ = event_opportunities(logits)
    if anchors.shape != opportunity.shape or anchors.dtype != torch.bool:
        raise ValueError('Expected a boolean endpoint mask matching logits')
    if margin <= 0:
        raise ValueError('Positive anchor margin required')
    penalty = F.relu(THRESHOLD_Q8 / 256 + margin - opportunity).square()
    loss = (penalty * anchors).sum() / anchors.sum().clamp_min(1)
    return loss, dict(anchor_penalty=loss.detach(), anchors=anchors.sum().detach())
