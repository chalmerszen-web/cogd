"""Variable-length whole-input CTC targets with a known blank prefix.

    OTHER is a catch-all training category for unknown negative inputs,
    not a claimed phonetic transcript. Only audited complete words have
    the explicit four-token language targets; only literal zero PCM is empty.
"""
import torch
from ctc_data import TOKENS


def validate_batch(logits, targets, lengths, known_blank_frames):
    if (logits.ndim != 3 or not len(logits) or logits.shape[1] != len(TOKENS) or
            logits.shape[-1] % 2 or not torch.isfinite(logits).all() or
            targets.shape != (len(logits), 4) or targets.dtype != torch.int64 or
            lengths.shape != (len(logits),) or lengths.dtype != torch.int64 or
            not isinstance(known_blank_frames, int) or known_blank_frames < 0 or
            known_blank_frames % 2 or known_blank_frames >= logits.shape[-1]):
        raise ValueError('Invalid finite CTC batch or known-blank prefix')
    if bool(((lengths < 0) | (lengths > 4)).any()):
        raise ValueError('Target length outside0..4')
    mask = torch.arange(4)[None] < lengths[:, None]
    if bool(((targets[mask] <= 0) | (targets[mask] >= len(TOKENS))).any()):
        raise ValueError('Expected active nonblank targets')
    if bool((targets[~mask] != 0).any()):
        raise ValueError('Inactive target padding must be0')
    frames = (logits.shape[-1] - known_blank_frames) // 2
    repeated = ((targets[:, 1:] == targets[:, :-1]) &
                (torch.arange(3)[None] + 1 < lengths[:, None])).sum(1)
    if bool((lengths + repeated > frames).any()):
        raise ValueError('Insufficient CTC frames')
    return frames


def sequence_loss(logits, targets, lengths, known_blank_frames=128):
    frames = validate_batch(logits, targets, lengths, known_blank_frames)
    logs = logits[:, :, 1::2].log_softmax(1)
    prefix_frames = known_blank_frames // 2
    suffix = logs[:, :, prefix_frames:].permute(2, 0, 1).contiguous()
    nll = torch.nn.functional.ctc_loss(suffix, targets,
        torch.full((len(logits),), frames, dtype=torch.int64), lengths,
        blank=0, reduction='none', zero_infinity=False)
    nll = nll - logs[:, 0, :prefix_frames].sum(1)
    return (nll / lengths.clamp_min(1)).mean()
