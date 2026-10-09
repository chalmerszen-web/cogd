"""Finite contract tests: no dataset, scores, fit, thresholds, USB or Flash."""
from pathlib import Path
import copy
import json
import sys
import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'training/kws'))
from data import framed_example
from periodic import PeriodicityModel, periodic_forward, TOPOLOGY, ENCODING
from phrase_labels import Placement, phrase_class, CLASSES, CONTRACT
from phrase_contrast import phrase_forward, phrase_loss, make_head, checkpoint_phrase


def verify():
    torch.set_num_threads(1)
    torch.manual_seed(20261002421)
    # Timing bookkeeping does not advance the augmentation RNG or alter PCM.
    row = dict(endpoint_schema=1, speech_end_low_sample=15000, speech_end_high_sample=15320,
               label=0, text='你好，小智', clip_id='fixture')
    original, actual = np.random.default_rng(421), np.random.default_rng(421)
    pcm = np.arange(16000, dtype=np.int16)
    expected = framed_example(row, pcm, original)
    placement = Placement(framed_example)
    got = placement(row, pcm, actual)
    for a, b in zip(expected, got):
        if a is None: assert b is None
        else: np.testing.assert_array_equal(a, b)
    assert actual.bit_generator.state == original.bit_generator.state
    assert placement.last['high']-placement.last['low'] == 320
    assert phrase_class(dict(label=1, text='你好，小言', sampling_group='positive')) == 0
    assert phrase_class(dict(label=0, text='你好，小言', sampling_group='paired_negative')) == -1
    assert phrase_class(dict(label=0, text='你好小智', sampling_group='natural_negative')) == -1
    assert phrase_class(dict(label=0, text='你好小智', sampling_group='hard_negative')) == CLASSES.index('你好小智')
    # The optional trace yields exactly the same backbone output.
    net = PeriodicityModel().eval()
    x = torch.randn(4, 90, 382)
    active = torch.zeros((4, 1, 382), dtype=torch.bool)
    active[:, :, 126:300] = True
    with torch.no_grad():
        binary = periodic_forward(net, x, active)
        traced, hidden = phrase_forward(net, x, active)
    assert torch.equal(binary, traced) and hidden.shape == (4, 24, 256)
    assert sum(p.numel() for p in net.parameters()) == 10273
    head = make_head()
    assert sum(p.numel() for p in head.parameters()) == 200
    features = torch.randn(4, 24, 256, requires_grad=True)
    labels = torch.tensor([0, 0, 6, -1])
    mask = torch.zeros((4, 256), dtype=torch.bool)
    mask[:3, 140:150] = True
    loss, details = phrase_loss(features, head, labels, mask)
    # Independent scalar/frame CE and per-class weighting.
    logits = head(features.transpose(1, 2))
    rows = []
    for i in range(3):
        frames = [torch.logsumexp(logits[i, f], dim=0)-logits[i, f, int(labels[i])] for f in range(140, 150)]
        rows.append(torch.stack(frames).mean())
    reference = ((rows[0]+rows[1])/2+rows[2])/2
    torch.testing.assert_close(loss, reference)
    loss.backward()
    assert details['phrase_rows'] == 3 and details['phrase_classes'] == 2
    assert features.grad[:3, :, 140:150].abs().sum() > 0
    assert not features.grad[3].any() and not features.grad[:, :, :140].any() and not features.grad[:, :, 150:].any()
    empty, _ = phrase_loss(features.detach(), head, torch.full((4,), -1), torch.zeros_like(mask))
    assert float(empty) == 0
    net.train()
    _, representation = phrase_forward(net, x, active)
    joint, _ = phrase_loss(representation, head, labels, mask)
    joint.backward()
    assert net.layers[0].conv.weight.grad.abs().sum() > 0
    assert net.layers[-1].conv.weight.grad is None  # head is on penultimate layer
    saved = dict(state_dict=net.state_dict(), auxiliary_state_dict=head.state_dict(), config=dict(
        topology=TOPOLOGY, pitch_encoding=ENCODING, inputs=90, channels=24,
        training_objective=CONTRACT, phrase_classes=list(CLASSES), phrase_weight=1., auxiliary_head_exported=False))
    checkpoint_phrase(saved).load_state_dict(saved['state_dict'])
    for key, value in (('training_objective', 'old_binary_recipe'), ('phrase_classes', []), ('auxiliary_head_exported', True), ('phrase_weight', 2.)):
        changed = copy.deepcopy(saved)
        changed['config'][key] = value
        try: checkpoint_phrase(changed)
        except ValueError: pass
        else: raise AssertionError('Invalid training metadata accepted: '+key)
    return dict(passed=True, original_PCM_and_RNG_exact=True, scalar_CE_class_balance_exact=True,
                ignored_and_invalid_frames_zero_gradient=True, backbone_shared_gradient=True,
                output_graph_unchanged=True, backbone_parameters=10273, auxiliary_train_parameters=200,
                strict_metadata_rejections=4, no_data_model_fit_or_quality_claim=True)


if __name__ == '__main__':
    print(json.dumps(verify()), flush=True)
