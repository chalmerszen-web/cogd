"""Registered pitched topology and continuous-context contracts."""
import unittest
import numpy as np
import torch
from pitched import (PitchedModel, PitchedContext, encode_pitch, checkpoint_pitched,
                     pitched_forward, TOPOLOGY, ENCODING)
from conditioned import checkpoint_conditioned


class PitchedTests(unittest.TestCase):
    def test_streaming_and_checkpoint(self):
        torch.manual_seed(20261003392)
        torch.set_num_threads(1)
        model = PitchedModel().eval()
        self.assertEqual(sum(p.numel() for p in model.parameters()), 10273)
        value = torch.randn(2, 90, 256)
        with torch.no_grad():
            reference = model(value)
            states, parts = None, []
            for frame in value.split(1, dim=-1):
                output, states = model.step(frame, states)
                parts.append(output)
            torch.testing.assert_close(torch.cat(parts, -1), reference, atol=1e-7, rtol=1e-5)
        saved = dict(state_dict=model.state_dict(), config=dict(topology=TOPOLOGY,
                     inputs=90, channels=24, pitch_encoding=ENCODING))
        self.assertIsInstance(checkpoint_pitched(saved), PitchedModel)
        with self.assertRaises(ValueError): checkpoint_conditioned(saved)
        saved['config']['pitch_encoding'] = 'other'
        with self.assertRaises(ValueError): checkpoint_pitched(saved)

    def test_context_and_gradient(self):
        parent = np.arange(400*40, dtype=np.int16).reshape(400, 40).astype(np.int8)
        hidden = np.full((400, 48), 17, np.int8)
        pitch = np.column_stack((np.arange(400)*16, np.full(400, 3000))).astype(np.int16)
        row = dict(index=0, data_key='natural', source_index=0, start_frame=120, real_frames=256)
        corpus = PitchedContext(parent[None, 120:376], hidden[None, 120:376], np.array([256]),
                   [row], {('natural', 0):(parent, hidden)}, pitch[None, 120:376], {('natural', 0):pitch})
        value, active = corpus.batch(np.array([0]))
        self.assertEqual(value.shape, (1, 382, 90))
        self.assertFalse(active[..., :6].any())
        self.assertTrue(active[..., 6:].all())
        np.testing.assert_array_equal(value[0, 6:, 88:], encode_pitch(pitch[:376]))
        model = PitchedModel().train()
        output = pitched_forward(model, torch.from_numpy(value.transpose(0, 2, 1).astype(np.float32)/32),
                                 torch.from_numpy(active))
        self.assertEqual(tuple(output.shape), (1, 1, 256))
        output.square().mean().backward()
        self.assertTrue(all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.parameters()))
        altered = pitch[None, 120:376].copy()
        altered[0, 0, 0] += 128
        with self.assertRaises(ValueError):
            PitchedContext(parent[None, 120:376], hidden[None, 120:376], np.array([256]),
                  [row], {('natural', 0):(parent, hidden)}, altered, {('natural', 0):pitch})

    def test_encoding(self):
        raw = np.array([[0, 0], [1280, 4096], [12800, 2048], [32767, 4095]], np.int16)
        np.testing.assert_array_equal(encode_pitch(raw), [[0, 0], [10, 127], [100, 64], [127, 127]])
        for invalid in (np.array([[-1, 0]], np.int16), np.array([[1, 4097]], np.int16), raw.astype(float)):
            with self.assertRaises(ValueError): encode_pitch(invalid)


if __name__ == '__main__': unittest.main()
