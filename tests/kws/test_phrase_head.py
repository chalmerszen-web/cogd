"""Projection arithmetic, class competition, gradients and ABI guards."""
from copy import deepcopy
from pathlib import Path
import sys
import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "training/kws"))
from phrase_head import IntegerPhraseHead, contrast, validate_layer


def oracle(representation, layer):
    weight = np.asarray(layer["weights"], dtype=np.int64).reshape(8, 24)
    acc = representation.astype(np.int64) @ weight.T + np.asarray(layer["bias"], dtype=np.int64)
    out = np.empty_like(acc)
    for i, shift in enumerate(layer["shift"]):
        value = acc[:, i]
        absolute = np.abs(value)
        rounded = ((absolute + (1 << (shift - 1))) >> shift) if shift > 0 else absolute << -shift
        out[:, i] = np.clip(np.sign(value) * rounded, -32768, 32767)
    score = np.clip(out[:, 0] - out[:, 1:].max(1), -32768, 32767)
    return np.column_stack((out, score)).astype(np.int16)


def verify(layer):
    rng = np.random.default_rng(20261003435)
    values = rng.integers(0, 128, size=(129, 24), dtype=np.int16).astype(np.float32)
    values[:3] = np.asarray([0, 127, 64])[:, None]
    tested = 0
    for shift in range(-31, 32):
        metadata = deepcopy(layer)
        metadata["shift"] = [shift] * 8
        net = IntegerPhraseHead(metadata)
        score, logits = net(torch.from_numpy(values.T[None]))
        actual = torch.cat((logits, score), 1)[0].T.detach().to(torch.int16).numpy()
        np.testing.assert_array_equal(actual, oracle(values, metadata))
        tested += actual.size
    tiny = dict(layer, bias=[0] * 8, shift=[5] * 8, weights=rng.integers(-4, 5, size=192).tolist())
    net = IntegerPhraseHead(tiny)
    value = torch.from_numpy(values[:8].T[None].copy()).requires_grad_(True)
    score, logits = net(value)
    loss = torch.nn.functional.cross_entropy(logits.transpose(1, 2).reshape(-1, 8) / 256,
                                            torch.arange(8)) + score.square().mean() / 65536
    loss.backward()
    assert torch.isfinite(net.layer.weight.grad).all()
    assert (net.layer.weight.grad.abs().sum((1, 2)) > 0).all()
    assert torch.isfinite(value.grad).all() and value.grad.abs().sum() > 0
    assert sum(p.numel() for p in net.parameters()) == 192
    assert net.export_layer() == tiny
    logits = torch.zeros(1, 8, 4)
    logits[:, 0] = 600
    logits[:, 1, 0], logits[:, 2, 1], logits[:, 7, 2] = 800, 600, 599
    torch.testing.assert_close(contrast(logits), torch.tensor([[[-200., 0., 1., 600.]]]))
    logits[:, 0, 0], logits[:, 1, 0] = 32767, -32768
    logits[:, 2:, 0] = -32768
    assert contrast(logits)[0, 0, 0] == 32767
    logits[:, 0, 0], logits[:, 1, 0] = -32768, 32767
    assert contrast(logits)[0, 0, 0] == -32768
    bad = []
    for key, value in (("inputs", 23), ("outputs", 7), ("kernel", 3), ("dilation", 2),
                       ("depthwise", True), ("relu", True), ("weights", [0] * 191),
                       ("bias", [0] * 7), ("shift", [0] * 7),
                       ("shift", [32] * 8), ("shift", [-32] * 8),
                       ("bias", [2**24 - 24 * 16384] * 8),
                       ("bias", [-2**24 + 24 * 16384] * 8),
                       ("weights", [128] * 192), ("weights", [0.] * 192)):
        item = deepcopy(layer)
        item[key] = value
        bad.append(item)
    for item in bad:
        try:
            validate_layer(item)
        except ValueError:
            pass
        else:
            raise AssertionError("Invalid phrase metadata accepted")
    try:
        IntegerPhraseHead(tiny, classes=("wrong",) * 8)
    except ValueError:
        pass
    else:
        raise AssertionError("Changed phrase identities accepted")
    for value in (torch.zeros(1, 23, 8), torch.zeros(1, 24, 8, dtype=torch.float64),
                  torch.full((1, 24, 8), -1.), torch.full((1, 24, 8), 128.),
                  torch.full((1, 24, 8), .25), torch.full((1, 24, 8), float("nan"))):
        try:
            net(value)
        except ValueError:
            pass
        else:
            raise AssertionError("Invalid phrase input accepted")
    return dict(passed=True, shifts=63, independent_oracle_values=tested, trainable_parameters=192,
                exact_export=True, finite_all_class_and_input_gradients=True,
                competition_and_signed_saturation=True, metadata_rejections=len(bad) + 1, input_rejections=6)
