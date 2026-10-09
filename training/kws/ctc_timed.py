"""Exact CTC path probability with an audited final-token time interval.

Only complete positive source words may use a time interval. Unknown speech
and near words keep their existing targets and all suffix frames. The bound
is training supervision, never a runtime timer or an invented phone timestamp.
"""
import numpy as np
import torch
from ctc_sequence import validate_batch


def audited_final_windows(start_accept, event_end, positive):
    """Map existing source annotations to inclusive512-sample suffix frames."""
    start_accept, event_end, positive = map(np.asarray, (start_accept, event_end, positive))
    if (start_accept.ndim != 1 or event_end.shape != start_accept.shape or
            positive.shape != start_accept.shape or start_accept.dtype != np.int64 or
            event_end.dtype != np.int64 or positive.dtype != np.bool_ or
            np.any(start_accept[positive] < 0) or
            np.any(event_end[positive] < start_accept[positive]) or
            np.any(event_end[positive] > 65536)):
        raise ValueError('Invalid original complete-word source boundaries')
    windows = np.full((len(positive), 2), -1, dtype=np.int64)
    windows[positive, 0] = (start_accept[positive] + 511) // 512 - 1
    windows[positive, 1] = np.minimum((event_end[positive] + 2560) // 512 - 1, 127)
    if (np.any(windows[positive, 0] < 0) or
            np.any(windows[positive, 1] < windows[positive, 0]) or
            np.any(windows[positive, 1] < 3)):
        raise ValueError('No complete-target path in the original source interval')
    return windows


def timed_sequence_loss(logits, targets, lengths, final_window,
                        known_blank_frames=128):
    frames = validate_batch(logits, targets, lengths, known_blank_frames)
    if bool((logits.abs() > 1024).any()):
        raise ValueError('Outside the finite bounded CTC numerical contract')
    if (final_window.shape != (len(logits), 2) or final_window.dtype != torch.int64):
        raise ValueError('Expected inclusive suffix-frame intervals, or -1/-1')
    active = final_window[:, 0] >= 0
    if (bool((final_window[~active] != -1).any()) or
            bool((final_window[active, 1] < final_window[active, 0]).any()) or
            bool((final_window[active, 1] >= frames).any()) or
            bool((final_window[active, 1] < 3).any()) or
            bool((lengths[active] != 4).any()) or
            bool((~((targets[active] == torch.tensor([1, 2, 3, 4])).all(1) |
                    (targets[active] == torch.tensor([5, 6, 7, 8])).all(1))).any())):
        raise ValueError('A unique positive final token and a feasible interval are required')
    prefix_frames = known_blank_frames // 2
    stride = logits[:, :, 1::2]
    suffix = stride[:, :, prefix_frames:]
    positions = torch.arange(frames)[None]
    outside = (positions < final_window[:, :1]) | (positions > final_window[:, 1:])
    token_ids = torch.arange(logits.shape[1])[None, :, None]
    blocked = active[:, None, None] & outside[:, None, :] & (
        token_ids == targets[:, 3, None, None])
    # Normalize the allowed classes for the stock CTC kernel. Removing this
    # per-frame normalization afterward retains the ORIGINAL path probability
    # and its gradient; renormalization alone would change the objective.
    # With raw logits within +/-1024, -4096 underflows to zero probability
    # even in float64. A literal -inf produces NaN in the CPU CTC backward.
    permitted = suffix.masked_fill(blocked, -4096.)
    allowed_logs = permitted.log_softmax(1)
    log_mass = permitted.logsumexp(1) - suffix.logsumexp(1)
    nll = torch.nn.functional.ctc_loss(allowed_logs.permute(2, 0, 1).contiguous(),
        targets, torch.full((len(logits),), frames, dtype=torch.int64), lengths,
        blank=0, reduction='none', zero_infinity=False)
    nll = nll - log_mass.sum(1)
    nll = nll - stride[:, :, :prefix_frames].log_softmax(1)[:, 0].sum(1)
    return (nll / lengths.clamp_min(1)).mean()
