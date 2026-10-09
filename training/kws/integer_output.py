"""Host-only bounded integer learning of a frozen C11 model's linear head."""
import numpy as np
from scipy.optimize import Bounds, LinearConstraint, milp


def solve(positive_sum, negative_sum, original, *, bias=-109, shift=2,
          threshold=268, margin=2, seconds=30):
    """One L1-nearest INT8 head; inputs sum three odd-frame hidden vectors.

    Integer constraints account for the original shift and final score
    rounding. The caller must still run complete temporal decisions and
    retention checks. A timeout/infeasible result is never a release model.
    """
    pos, neg, old = map(np.asarray, (positive_sum, negative_sum, original))
    if (pos.ndim != 2 or neg.ndim != 2 or pos.shape[1:] != (48,)
            or neg.shape[1:] != (48,) or not len(pos) or not len(neg)
            or old.shape != (48,) or any(a.dtype.kind not in 'iu' for a in (pos,neg,old))
            or (old < -128).any() or (old > 127).any()
            or any((a < 0).any() or (a > 381).any() for a in (pos,neg))
            or (bias,shift,threshold,margin,seconds) != (-109,2,268,2,30)):
        raise ValueError('Unregistered frozen48 output-layer contract')
    unit = 3 * (1 << shift)
    eye = np.eye(48)
    matrix = np.concatenate((np.c_[pos,np.zeros_like(pos)],np.c_[neg,np.zeros_like(neg)],
                             np.c_[eye,-eye],np.c_[-eye,-eye]))
    low = np.r_[np.full(len(pos),(threshold+margin)*unit-3*bias),
                np.full(len(neg)+96,-np.inf)]
    high = np.r_[np.full(len(pos),np.inf),
                 np.full(len(neg),(threshold-margin)*unit-3*bias),old,-old]
    return milp(c=np.r_[np.zeros(48),np.ones(48)],
        integrality=np.r_[np.ones(48,dtype=np.int8),np.zeros(48,dtype=np.int8)],
        bounds=Bounds(np.r_[np.full(48,-128),np.zeros(48)],
                      np.r_[np.full(48,127),np.full(48,255)]),
        constraints=LinearConstraint(matrix,low,high),
        options={'time_limit':seconds,'mip_rel_gap':0.,'presolve':True})
