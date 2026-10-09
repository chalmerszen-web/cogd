"""Training-only known-silence path constraint; no runtime clock rule."""
import numpy as np
import torch
from ctc_model import loss


def prepend_silence(values, silence, frames=128):
    values, silence = np.asarray(values), np.asarray(silence)
    if (values.ndim != 3 or values.shape[2] != 40 or values.dtype != np.int8 or
            silence.ndim != 2 or silence.shape[1] != 40 or silence.dtype != np.int8 or
            not isinstance(frames, int) or frames <= 0 or frames % 2 or frames > len(silence)):
        raise ValueError('Expected INT8 batch x frame x40 and verified even silence prefix')
    prefix = np.broadcast_to(silence[:frames], (len(values), frames, 40))
    return np.concatenate((prefix, values), axis=1)


def prefix_loss(logits, targets, known_blank_frames=128):
    """Exact path NLL: prescribed blanks, then unconstrained suffix CTC.

    Frame count uses16ms model outputs, including the odd512-sample stride.
    Callers must provide a genuine added zero-PCM prefix; this must never be
    inferred for arbitrary speech or used to mask microphone outputs.
    """
    if (not isinstance(known_blank_frames, int) or known_blank_frames <= 0 or
            known_blank_frames % 2 or logits.ndim != 3 or
            known_blank_frames >= logits.shape[-1] or not torch.isfinite(logits).all()):
        raise ValueError('Expected finite logits and nonempty even known-blank prefix')
    suffix = loss(logits[:, :, known_blank_frames:], targets)
    prefix = logits[:, :, 1:known_blank_frames:2].log_softmax(1)
    # loss() normalizes by four target tokens, so use the same exact factor.
    prescribed = -prefix[:, 0].sum(1).mean() / 4
    return prescribed + suffix
