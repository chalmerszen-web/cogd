"""Positive-anchor supervision for the fixed negative-evidence reset prototype.

This is a host-only experiment, not the deployed decoder or default loss.
Keep existing negative and prefix penalties unchanged. The caller supplies
the existing accepted_anchor_mask, which excludes negatives and padded tails.
"""
import torch
from torch.nn import functional as F
from event_loss import decision_scores, THRESHOLD_Q8


def revocation_opportunities(logits):
    """New two-of-three support, with no negative score after its first vote.

For a,b,c and threshold T, legal pairs are ab with c>=0, bc, or ac with
b>=0. Taking the maximum of their gated minima gives an exact forward
threshold test; it never exceeds the ordinary three-score median.
This represents newly recorded support, not the full pending-owner policy.
"""
    scores = decision_scores(logits)
    window = scores.unfold(1, 3, 1)
    a, b, c = window.unbind(-1)
    ab = torch.minimum(torch.minimum(a, b), c + THRESHOLD_Q8)
    bc = torch.minimum(b, c)
    ac = torch.minimum(torch.minimum(a, c), b + THRESHOLD_Q8)
    opportunity = torch.maximum(torch.maximum(ab, bc), ac) / 256
    end = (torch.arange(opportunity.shape[1], device=logits.device) + 3) * 512
    return opportunity, end


def revocation_anchor_loss(logits, anchors, margin=1.):
    opportunity, _ = revocation_opportunities(logits)
    if anchors.shape != opportunity.shape or anchors.dtype != torch.bool:
        raise ValueError('Expected a boolean accepted-endpoint mask matching logits')
    if margin <= 0:
        raise ValueError('Positive anchor margin required')
    penalty = F.relu(THRESHOLD_Q8 / 256 + margin - opportunity).square()
    loss = (penalty * anchors).sum() / anchors.sum().clamp_min(1)
    return loss, dict(revocation_anchor_penalty=loss.detach(), anchors=anchors.sum().detach())
