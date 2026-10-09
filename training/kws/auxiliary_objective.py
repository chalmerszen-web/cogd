"""Train support for the fixed primary-two/auxiliary-one causal detector.

Eligibility is a necessary condition before vote resets, cooldown and arming.
It is deliberately a superset of accepted events, not a detector replacement.
Inputs are already smoothed logits; padded or pre-warm samples never contribute.
"""
import numpy as np
import torch
from torch.nn import functional as F


def strength(scores):
    """One supporting high logit in the same three-block window."""
    return scores.unfold(1, 3, 1).max(dim=-1).values


def make_masks(scores, label, low, high, length):
    from pair_qat import events

    scores = np.asarray(scores)
    label = np.asarray(label, dtype=bool)
    low, high, length = (np.asarray(x) for x in (low, high, length))
    if scores.ndim != 2 or scores.shape[1] < 3:
        raise ValueError('Expected at least three smoothed blocks per stream')
    if any(x.shape != (len(scores),) for x in (label, low, high, length)):
        raise ValueError('Expected one label, interval and real length per stream')
    if np.any(length < 0) or np.any(length > scores.shape[1]*2):
        raise ValueError('Real frame length is outside the supplied score buffer')
    ends = np.arange(3, scores.shape[1]+1)*512
    warm = (ends[None, :] >= 32768) & (ends[None, :] <= length[:, None]*256)
    primary = np.sort(np.lib.stride_tricks.sliding_window_view(scores, 3, axis=1), axis=-1)[:, :, 1]
    eligible = warm & (primary >= 268)
    valid = eligible & (ends[None, :] >= low[:, None]) & (ends[None, :] <= high[:, None]+12800)
    early = eligible & (ends[None, :] < low[:, None])
    anchor = np.zeros(len(scores), dtype=np.int64)
    hit = np.zeros(len(scores), dtype=bool)
    triggered = np.zeros(len(scores), dtype=bool)
    target = np.full(len(scores), 332., dtype=np.float32)
    for i in range(len(scores)):
        detected = events(scores[i, :int(length[i])//2])
        triggered[i] = bool(detected)
        accepted = [end for end in detected if low[i] <= end <= high[i]+12800]
        if label[i] and accepted:
            hit[i] = True
            anchor[i] = accepted[0]//512-3
            assert valid[i, anchor[i]]
            target[i] = min(332, primary[i, anchor[i]])
        elif label[i] and valid[i].any():
            anchor[i] = np.where(valid[i], primary[i], -65536).argmax()
    return dict(eligible=eligible, valid=valid, early=early, anchor=anchor,
        target=target, hit=hit, triggered=triggered,
        train_positive=valid.any(axis=1),
        ceiling=np.full(len(scores), 252., dtype=np.float32),
        before_ceiling=np.full(len(scores), 252., dtype=np.float32))


def _peak(values, mask):
    return values.masked_fill(~mask, -65536.).max(dim=1).values


def objective(votes, positive, masks, multiplier):
    """Finite masked loss; impossible positives are excluded, not relabelled.

    Soft optimization does not guarantee admission; the unchanged C detector
    and all retention/error/timing gates must still be evaluated afterwards.
    """
    rows = torch.arange(votes.shape[0], device=votes.device)
    at_anchor = votes[rows, masks['anchor']]
    positive_violation = F.relu((masks['target']-at_anchor)/256)*masks['hit']
    positive_loss = torch.where(masks['hit'], multiplier[:, 0]*positive_violation,
        F.softplus((332-_peak(votes, masks['valid']))/256))
    positive_loss = torch.where(masks['train_positive'], positive_loss, 0.)
    peak = _peak(votes, masks['eligible'])
    negative_violation = F.relu((peak-masks['ceiling'])/256)
    negative_loss = F.softplus((peak-204)/256)+multiplier[:, 0]*negative_violation
    negative_loss = torch.where(masks['eligible'].any(dim=1), negative_loss, 0.)
    pre = _peak(votes, masks['early'])
    pre_violation = F.relu((pre-masks['before_ceiling'])/256)
    loss = torch.where(positive, positive_loss, negative_loss).mean()
    loss += torch.where(positive, multiplier[:, 1]*pre_violation, 0.).mean()
    residual = torch.stack((torch.where(positive, positive_violation, negative_violation),
        torch.where(positive, pre_violation, 0.)), dim=1)
    return loss, residual
