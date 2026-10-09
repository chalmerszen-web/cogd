"""Host-only named-word supervision; no invented phone or tone labels.

Source-supported complete names are distinct semantic acoustic events.
Unknown/incomplete negative speech has no such event, not a transcript.
Only the final two rows are projected to the existing C11 GRU32 runtime.
"""
import numpy as np
import torch
from torch import nn
from recurrent import Recurrent32
from ctc_data import WORDS
from word_event import event_loss

CONTRACT = 'xiaoyan_gru32_named9_event2_host_projection_v1'
TOKENS = ('no_named_word_event',) + tuple(WORDS)
WORD_CLASSES = len(TOKENS)


class RecurrentWordJoint32(Recurrent32):
    def __init__(self):
        super().__init__()
        self.output = nn.Linear(32, WORD_CLASSES + 2)


def source_targets(class_ids, positive):
    """Use the existing audited source word IDs; -1 is unknown/incomplete."""
    ids, positive = np.asarray(class_ids), np.asarray(positive)
    if (ids.ndim != 1 or ids.dtype.kind != 'i' or positive.shape != ids.shape or
            positive.dtype != np.bool_ or not np.isin(ids, range(-1, len(WORDS))).all() or
            not np.array_equal(positive, ids == 0)):
        raise ValueError('Original audited word IDs and positive truth required')
    targets = np.maximum(ids.astype(np.int64) + 1, 0)[:, None]
    lengths = (ids >= 0).astype(np.int64)
    return targets, lengths


def named_loss(logits, targets, lengths, windows, known_blank_frames=128):
    """Exact one-word CTC probability, preserving original normalization.

    Only the positive keyword uses the audited final-event interval. A known
    near word has its own class and all suffix frames. The zero PCM prefix
    permits only blank; no letter is synthesized from a shared silent prefix.
    """
    if (logits.ndim != 3 or not len(logits) or logits.shape[1] != WORD_CLASSES or
            logits.shape[2] % 2 or not torch.isfinite(logits).all() or
            bool((logits.abs() > 1024).any()) or targets.shape != (len(logits), 1) or
            targets.dtype != torch.int64 or lengths.shape != (len(logits),) or
            lengths.dtype != torch.int64 or bool(((lengths < 0) | (lengths > 1)).any()) or
            bool(((targets < 0) | (targets >= WORD_CLASSES)).any()) or
            not bool(torch.equal(lengths, (targets[:, 0] > 0).long())) or
            windows.shape != (len(logits), 2) or windows.dtype != torch.int64 or
            type(known_blank_frames) is not int or known_blank_frames < 0 or
            known_blank_frames % 2 or known_blank_frames >= logits.shape[2]):
        raise ValueError('Invalid finite named-word CTC batch')
    active = targets[:, 0] == 1
    prefix_frames = known_blank_frames // 2
    stride = logits[:, :, 1::2]
    suffix = stride[:, :, prefix_frames:]
    frames = suffix.shape[-1]
    if (bool((windows[~active] != -1).any()) or
            bool((windows[active, 0] < 0).any()) or
            bool((windows[active, 1] < windows[active, 0]).any()) or
            bool((windows[active, 1] >= frames).any())):
        raise ValueError('Original keyword interval required; near words stay unrestricted')
    positions = torch.arange(frames, device=logits.device)[None]
    outside = (positions < windows[:, :1]) | (positions > windows[:, 1:])
    blocked = torch.zeros_like(suffix, dtype=torch.bool)
    blocked[:, 1] = active[:, None] & outside
    permitted = suffix.masked_fill(blocked, -4096.)
    original_mass = permitted.logsumexp(1) - suffix.logsumexp(1)
    nll = torch.nn.functional.ctc_loss(permitted.log_softmax(1).permute(2, 0, 1).contiguous(),
        targets, torch.full((len(logits),), frames, dtype=torch.int64), lengths,
        blank=0, reduction='none', zero_infinity=False)
    nll = nll - original_mass.sum(1) - stride[:, :, :prefix_frames].log_softmax(1)[:, 0].sum(1)
    return nll.mean()


def joint_loss(logits, words, word_lengths, events, event_lengths, windows,
               known_blank_frames=128):
    if logits.ndim != 3 or logits.shape[1] != WORD_CLASSES + 2:
        raise ValueError('Named9 + original event2 outputs required')
    return (named_loss(logits[:, :WORD_CLASSES], words, word_lengths, windows,
                       known_blank_frames) +
            event_loss(logits[:, WORD_CLASSES:], events, event_lengths, windows,
                       known_blank_frames)) / 2


def event_projection(network):
    if not isinstance(network, RecurrentWordJoint32) or network.training:
        raise ValueError('Frozen named-word GRU32 required')
    state = {name: value.detach().clone() for name, value in network.state_dict().items()}
    state['output.weight'] = state['output.weight'][WORD_CLASSES:].clone()
    state['output.bias'] = state['output.bias'][WORD_CLASSES:].clone()
    output = network.output.weight
    projected = Recurrent32().to(device=output.device, dtype=output.dtype).eval()
    projected.load_state_dict(state, strict=True)
    return projected
