"""Discardable eight-class TRAIN head; the deployed90-input graph is unchanged."""
import hashlib
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

from data import ROOT
from periodic import checkpoint_periodic, periodic_forward
from periodic_calibrate import calibrate_periodic
from phrase_labels import CLASSES, CONTRACT


def phrase_forward(network, value, active):
    output, traces = periodic_forward(network, value, active, trace=True)
    return output, traces[-2]


def make_head():
    return nn.Linear(24, len(CLASSES))


def phrase_loss(features, head, class_id, mask):
    """Mean per phrase/frame, then per present class; missing classes are ignored."""
    if features.ndim != 3 or features.shape[1:] != (24, 256):
        raise ValueError('Expected local256x24 penultimate representation')
    if class_id.shape != (len(features),) or class_id.dtype != torch.int64:
        raise ValueError('Expected integer full-phrase classes')
    if mask.shape != (len(features), 256) or mask.dtype != torch.bool:
        raise ValueError('Expected matching real-frame phrase mask')
    if bool(((class_id < -1) | (class_id >= len(CLASSES))).any()):
        raise ValueError('Unknown full-phrase class')
    if not torch.equal(mask.any(1), class_id >= 0):
        raise ValueError('Phrase mask and complete-phrase labels differ')
    logits = head(features.transpose(1, 2))
    selected = class_id >= 0
    if not bool(selected.any()):
        return logits.sum()*0, dict(phrase_loss=0., phrase_rows=0, phrase_classes=0)
    labels = class_id[selected, None].expand(-1, 256)
    per_frame = F.cross_entropy(logits[selected].transpose(1, 2), labels, reduction='none')
    per_row = (per_frame*mask[selected]).sum(1)/mask[selected].sum(1)
    values = [per_row[class_id[selected] == k].mean() for k in range(len(CLASSES)) if bool((class_id[selected] == k).any())]
    loss = torch.stack(values).mean()
    return loss, dict(phrase_loss=float(loss.detach()), phrase_rows=int(selected.sum()), phrase_classes=len(values))


def checkpoint_phrase(saved):
    config = saved.get('config', {})
    if (config.get('training_objective') != CONTRACT or config.get('phrase_classes') != list(CLASSES)
            or config.get('phrase_weight') != 1.0 or config.get('auxiliary_head_exported') is not False):
        raise ValueError('Unregistered full-phrase training contract')
    auxiliary = saved.get('auxiliary_state_dict', {})
    if set(auxiliary) != {'weight', 'bias'} or tuple(auxiliary['weight'].shape) != (8, 24) or tuple(auxiliary['bias'].shape) != (8,):
        raise ValueError('Missing or incorrect TRAIN-only head')
    if not all(torch.isfinite(value).all() for value in auxiliary.values()):
        raise ValueError('Nonfinite TRAIN-only head')
    return checkpoint_periodic(saved)


def calibrate_phrase(checkpoint, features, corpus, maximum=512):
    saved = torch.load(checkpoint, map_location='cpu', weights_only=True)
    checkpoint_phrase(saved)
    descriptor = saved['config']['phrase_labels']
    with (ROOT/descriptor['path']).open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != descriptor['sha256']:
            raise ValueError('Changed full-phrase supervision')
    result = calibrate_periodic(checkpoint, features, corpus, maximum)
    assert len(result['layers']) == 12 and sum(len(layer['weights']) for layer in result['layers']) == 9984
    result.update(training_objective=CONTRACT, phrase_classes=list(CLASSES), phrase_labels=descriptor,
                  auxiliary_head_exported=False, auxiliary_train_parameters=200)
    return result
