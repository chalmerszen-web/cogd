"""Finite, host-only feasibility of a frozen 48-channel output head.

These are affine necessary bounds, not a replacement for the C event test.
The caller freezes support/guard selection before invoking either solver.
"""
import numpy as np
from scipy.optimize import Bounds, LinearConstraint, linprog, milp
from scipy.sparse import csr_matrix, hstack, vstack


def problem(sums, lower, upper):
    x, lo, hi = map(np.asarray, (sums, lower, upper))
    if (x.ndim != 2 or x.shape[1] != 48 or x.dtype.kind not in 'iu'
            or (x < 0).any() or (x > 381).any()
            or lo.shape != (len(x),) or hi.shape != lo.shape
            or (lo > hi).any() or np.isnan(lo).any() or np.isnan(hi).any()
            or not np.all(np.isfinite(lo) ^ np.isfinite(hi))):
        raise ValueError('Expected single-sided, frozen 3-frame INT8 sums')
    # w, positive weight parts, negative weight parts. The last two bounds
    # keep every possible hidden vector in the unclipped INT16 head range.
    eye = csr_matrix(np.eye(48)); zero = csr_matrix((48, 48))
    structural = vstack((hstack((eye, -eye, zero)),
                         hstack((-eye, zero, -eye)),
                         csr_matrix(np.r_[np.zeros(48), np.ones(48), np.zeros(48)][None]),
                         csr_matrix(np.r_[np.zeros(96), np.ones(48)][None])), format='csr')
    structural_upper = np.r_[np.zeros(96), 1032., 1031.]
    data = hstack((csr_matrix(x), csr_matrix((len(x), 96))), format='csr')
    bounds = Bounds(np.r_[np.full(48, -128.), np.zeros(96)],
                    np.r_[np.full(48, 127.), np.full(96, 128.)])
    return data, lo, hi, structural, structural_upper, bounds


def feasibility(sums, lower, upper, seconds=30):
    """One minimax LP. Positive optimum rejects only the frozen contract."""
    if seconds != 30:
        raise ValueError('Unregistered feasibility budget')
    data, lo, hi, structural, limit, bounds = problem(sums, lower, upper)
    sign = np.where(np.isfinite(hi), 1., -1.)
    signed = data.multiply(sign[:, None])
    rhs = np.where(np.isfinite(hi), hi, -lo)
    matrix = vstack((hstack((signed, -csr_matrix(np.ones((len(lo), 1))))),
                     hstack((structural, csr_matrix((98, 1))))), format='csr')
    rhs = np.r_[rhs, limit]
    lb, ub = np.r_[bounds.lb, 0.], np.r_[bounds.ub, np.inf]
    objective = np.r_[np.zeros(144), 1.]
    found = linprog(objective, A_ub=matrix, b_ub=rhs,
                    bounds=np.c_[lb, ub], method='highs',
                    options={'time_limit': seconds, 'presolve': True})
    return found, matrix, rhs, lb, ub, objective


def integer_proposal(sums, lower, upper, original, seconds=30):
    """Conditional single INT8 L1 solve; only a C-validated optimum is usable."""
    old = np.asarray(original)
    if seconds != 30 or old.shape != (48,) or old.dtype.kind not in 'iu':
        raise ValueError('Unregistered integer proposal')
    data, lo, hi, structural, limit, bounds = problem(sums, lower, upper)
    eye = csr_matrix(np.eye(48)); zero = csr_matrix((48, 96))
    absolute = vstack((hstack((eye, zero, -eye)), hstack((-eye, zero, -eye))), format='csr')
    matrix = vstack((hstack((data, csr_matrix((len(lo), 48)))),
                     hstack((structural, csr_matrix((98, 48)))), absolute), format='csr')
    low = np.r_[lo, np.full(98 + 96, -np.inf)]
    high = np.r_[hi, limit, old, -old]
    return milp(np.r_[np.zeros(144), np.ones(48)],
                integrality=np.r_[np.ones(48), np.zeros(144)],
                bounds=Bounds(np.r_[bounds.lb, np.zeros(48)], np.r_[bounds.ub, np.full(48, 255.)]),
                constraints=LinearConstraint(matrix, low, high),
                options={'time_limit': seconds, 'presolve': True, 'mip_rel_gap': 0.})
