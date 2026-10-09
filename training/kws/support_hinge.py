"""Stop negative optimization once the declared safety constraint is met.

The masks, positive teacher, preword constraints and detector are unchanged.
This is a training-only alternative; it has no firmware runtime dependency.
"""
import torch
from torch.nn import functional as F
from auxiliary_objective import make_masks, strength, _peak


def objective(votes, positive, masks, multiplier):
    rows = torch.arange(votes.shape[0], device=votes.device)
    at_anchor = votes[rows, masks['anchor']]
    positive_violation = F.relu((masks['target']-at_anchor)/256)*masks['hit']
    positive_loss = torch.where(masks['hit'], multiplier[:, 0]*positive_violation,
        F.softplus((332-_peak(votes, masks['valid']))/256))
    positive_loss = torch.where(masks['train_positive'], positive_loss, 0.)
    peak = _peak(votes, masks['eligible'])
    negative_violation = F.relu((peak-masks['ceiling'])/256)
    negative_loss = multiplier[:, 0]*negative_violation
    pre = _peak(votes, masks['early'])
    pre_violation = F.relu((pre-masks['before_ceiling'])/256)
    loss = torch.where(positive, positive_loss, negative_loss).mean()
    loss += torch.where(positive, multiplier[:, 1]*pre_violation, 0.).mean()
    residual = torch.stack((torch.where(positive, positive_violation, negative_violation),
        torch.where(positive, pre_violation, 0.)), dim=1)
    return loss, residual
