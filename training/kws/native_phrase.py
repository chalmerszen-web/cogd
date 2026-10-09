"""Native eight-class integer projection with a shared causal C11 backbone.

The old scalar layer remains a frozen ABI placeholder. Decisions use the
exported class0-minus-strongest-near-word score, never that scalar output.
"""
from copy import deepcopy
import torch
from torch import nn
from torch.nn import functional as F

from conditioned_integer_qat import IntegerConditioned
from contextual import CONTEXT_FRAMES
from phrase_head import IntegerPhraseHead, CONTRACT as HEAD_CONTRACT
from phrase_labels import CLASSES

CONTRACT = "c_exact90_native_phrase8_retained_contrast_fixed_scales_v1"
TRAINABLE_WEIGHTS = 10152


class NativePhraseConditioned(nn.Module):
    def __init__(self, model, head):
        super().__init__()
        self.backbone = IntegerConditioned(model)
        self.backbone.layers[-1].weight.requires_grad_(False)
        self.phrase = IntegerPhraseHead(head)

    def outputs(self, value, active=None, *, trace=False):
        _, layers = self.backbone.features(value, active, trace=True)
        score, logits = self.phrase(layers[-2])
        if active is not None:
            score = score.masked_fill(~active, 0)
            logits = logits.masked_fill(~active, 0)
        return (score, logits, layers) if trace else (score, logits)

    def export_model(self):
        result = self.backbone.export_model()
        result.update(training_objective=CONTRACT, native_phrase_contract=HEAD_CONTRACT,
                      native_phrase_classes=list(CLASSES),
                      native_phrase_layer=self.phrase.export_layer(),
                      native_phrase_head_retained=True,
                      scalar_ABI_layer_frozen_and_unused_for_decision=True,
                      trainable_integer_weights=TRAINABLE_WEIGHTS)
        return result


def native_phrase_context_forward(network, value, active):
    if value.ndim != 3 or value.shape[1:] != (90, CONTEXT_FRAMES + 256):
        raise ValueError("Expected the registered contextual90 input")
    score, logits = network.outputs(value * 32, active)
    return score[:, :, CONTEXT_FRAMES:] / 256, logits[:, :, CONTEXT_FRAMES:] / 256


def native_phrase_loss(logits, class_id, mask):
    """Balance present classes, then phrases; preserve the original tail mask."""
    if logits.ndim != 3 or logits.shape[1:] != (8, 256):
        raise ValueError("Expected local256x8 Q8-scaled phrase logits")
    if class_id.shape != (len(logits),) or class_id.dtype != torch.int64:
        raise ValueError("Expected integer complete-phrase classes")
    if mask.shape != (len(logits), 256) or mask.dtype != torch.bool:
        raise ValueError("Expected matching real-frame phrase mask")
    if bool(((class_id < -1) | (class_id >= 8)).any()):
        raise ValueError("Unknown full-phrase class")
    if not torch.equal(mask.any(1), class_id >= 0):
        raise ValueError("Phrase mask and complete-phrase identity differ")
    selected = class_id >= 0
    if not bool(selected.any()):
        return logits.sum() * 0, dict(phrase_loss=0., phrase_rows=0, phrase_classes=0)
    labels = class_id[selected, None].expand(-1, 256)
    frame_loss = F.cross_entropy(logits[selected], labels, reduction="none")
    row_loss = (frame_loss * mask[selected]).sum(1) / mask[selected].sum(1)
    values = [row_loss[class_id[selected] == k].mean() for k in range(8)
              if bool((class_id[selected] == k).any())]
    loss = torch.stack(values).mean()
    return loss, dict(phrase_loss=float(loss.detach()), phrase_rows=int(selected.sum()),
                      phrase_classes=len(values))


def checkpoint_native(saved):
    config = saved.get("config", {})
    if (config.get("training_objective") != CONTRACT or
            config.get("native_phrase_classes") != list(CLASSES) or
            config.get("phrase_weight") != 1.0 or
            config.get("native_phrase_head_retained") is not True or
            config.get("post_training_calibrations") != 0):
        raise ValueError("Unregistered native phrase checkpoint")
    network = NativePhraseConditioned(saved["initial_integer_model"], saved["initial_phrase_layer"])
    state = saved.get("state_dict", {})
    if state.keys() != network.state_dict().keys():
        raise ValueError("Unexpected native phrase checkpoint fields")
    for key, initial in network.state_dict().items():
        value = state[key]
        if value.shape != initial.shape or not torch.isfinite(value).all():
            raise ValueError("Invalid native checkpoint tensor: " + key)
        if (key.endswith((".bias", ".scale")) or key == "backbone.layers.11.weight"):
            if not torch.equal(value, initial):
                raise ValueError("Frozen native checkpoint tensor changed: " + key)
    network.load_state_dict(state)
    return network


def emit_native_head(layer):
    """Separate const descriptor keeps the old12-layer model ABI intact."""
    IntegerPhraseHead(layer)
    parts = ['#include "phrase_head.h"']
    for key, ctype in (("weights", "int8_t"), ("bias", "int32_t"), ("shift", "int8_t")):
        parts.append(f"static const {ctype} native_phrase_{key}[{len(layer[key])}] = {{"
                     + ",".join(map(str, layer[key])) + "};")
    parts.append("const kws_layer_t kws_native_phrase_layer = {native_phrase_weights,"
                 "native_phrase_bias,native_phrase_shift,24,8,1,1,0,0};")
    return "\n".join(parts) + "\n"
