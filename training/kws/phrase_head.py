"""C-exact eight-class phrase projection. This is a new optional deployment ABI.

No trained phrase head is supplied. The existing single-output network and
rejected fits are unchanged; the caller controls backbone learning and losses.
"""
from copy import deepcopy
import torch
from torch import nn
from conditioned_integer_qat import IntegerLayer
from pair_qat import round_away
from phrase_labels import CLASSES

CONTRACT = "phrase8_int8_penultimate24_keyword_minus_max_near_q8_v1"


def validate_layer(layer):
    expected = (24, 8, 1, 1, False, False)
    if tuple(layer.get(k) for k in ("inputs", "outputs", "kernel", "dilation", "depthwise", "relu")) != expected:
        raise ValueError("Expected the registered24-to8 phrase projection")
    for key, length in (("weights", 192), ("bias", 8), ("shift", 8)):
        values = layer.get(key, ())
        if len(values) != length or any(type(value) is not int for value in values):
            raise ValueError("Invalid integer phrase array: " + key)
    if any(not -128 <= value <= 127 for value in layer["weights"]):
        raise ValueError("Phrase weight outside INT8")
    if any(not -31 <= value <= 31 for value in layer["shift"]):
        raise ValueError("Phrase shift outside C representation")
    margin = 24 * 16384
    if any(abs(value) + margin >= 2**24 for value in layer["bias"]):
        raise ValueError("Phrase accumulator exceeds exact FP32 bound")


def contrast(logits):
    if logits.ndim != 3 or logits.shape[1] != 8:
        raise ValueError("Expected batch x8 xframes integer logits")
    return (logits[:, :1] - logits[:, 1:].amax(1, keepdim=True)).clamp(-32768, 32767)


class IntegerPhraseHead(nn.Module):
    def __init__(self, layer, classes=CLASSES):
        super().__init__()
        if tuple(classes) != CLASSES:
            raise ValueError("Phrase class order changed")
        validate_layer(layer)
        self.metadata = deepcopy(layer)
        self.layer = IntegerLayer(layer, 11)

    def forward(self, representation):
        if representation.ndim != 3 or representation.shape[1] != 24 or not representation.shape[0] or not representation.shape[2]:
            raise ValueError("Expected nonempty batch x24 xframes")
        if representation.dtype != torch.float32:
            raise ValueError("Phrase integer accumulation requires float32")
        raw = representation.detach()
        if (not bool(torch.isfinite(raw).all()) or bool(((raw < 0) | (raw > 127)).any())
                or not torch.equal(raw, raw.round())):
            raise ValueError("Expected the existing ReLU INT8 representation")
        logits = self.layer.quantize(self.layer.accumulate(representation))
        return contrast(logits), logits

    def export_layer(self):
        result = deepcopy(self.metadata)
        weight = round_away(self.layer.weight.detach().clamp(-128, 127)).to(torch.int8)
        result["weights"] = weight.permute(0, 2, 1).contiguous().flatten().cpu().tolist()
        validate_layer(result)
        return result
