import sys
import unittest
from pathlib import Path
import numpy as np
import torch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'training/kws'))
from ctc_data import WORDS, TOKENS, token_target
from ctc_model import CTCModel, collapse, loss, quantize_model, emit_c, integer_logits


class CTCTest(unittest.TestCase):
    def setUp(self):
        torch.set_num_threads(1)
        torch.manual_seed(447)

    def test_targets(self):
        for language in ('zh', 'yue'):
            for word in WORDS:
                target = token_target(word, language, full_phrase=True)
                self.assertEqual(len(target), 4)
                self.assertTrue(all(0 < token < 10 for token in target))
        self.assertEqual(token_target('你好，小言', 'zh', full_phrase=True), (1, 2, 3, 4))
        self.assertEqual(token_target('你好小燕', 'yue', full_phrase=True), (5, 6, 7, 9))
        for text, language, full in [('你好', 'zh', True), ('小言', 'yue', True),
                ('你好小言', 'en', True), ('你好小言', 'zh', False)]:
            with self.assertRaises(ValueError): token_target(text, language, full_phrase=full)
        self.assertEqual(collapse([0, 1, 1, 0, 1, 2, 2, 0]), [1, 1, 2])

    def test_stream_and_loss(self):
        net = CTCModel().eval()
        self.assertEqual(sum(p.numel() for p in net.parameters()), 19742)
        value = torch.randn(2, 40, 32)
        expected = net(value)
        states, parts = None, []
        for part in value.split(1, dim=-1):
            output, states = net.step(part, states); parts.append(output)
        torch.testing.assert_close(torch.cat(parts, -1), expected, rtol=1e-5, atol=1e-6)
        target = torch.tensor([[1, 2, 3, 4], [5, 6, 7, 8]])
        objective = loss(net(value), target)
        objective.backward()
        self.assertTrue(torch.isfinite(objective))
        self.assertGreater(float(net.layers[0].conv.weight.grad.abs().sum()), 0)
        self.assertGreater(float(net.layers[-1].conv.weight.grad.abs().sum()), 0)
        with self.assertRaises(ValueError): loss(expected, torch.zeros_like(target))
        with self.assertRaises(ValueError): loss(expected[:, :, :8], torch.tensor([[1, 2, 9, 9]] * 2))

    def test_export_retains_tokens_and_disarms_scalar(self):
        net = CTCModel().eval()
        bundle = quantize_model(net, torch.randn(2, 40, 32),
            dict(mean_q8=[0] * 40, inverse_std_q12=[4096] * 40))
        self.assertEqual(bundle['tokens'], list(TOKENS))
        self.assertEqual(bundle['token_head']['outputs'], 14)
        self.assertEqual(len(bundle['token_head']['weights']), 48 * 14)
        self.assertFalse(bundle['backbone']['trained'])
        self.assertEqual(bundle['backbone']['layers'][-1]['weights'], [0] * 48)
        text = emit_c(bundle)
        self.assertIn('inverse_std,false}', text)
        self.assertIn('kws_ctc_token_layer=', text)

    def test_contiguous_FFI_rows(self):
        raw = np.arange(14 * 16, dtype=np.float32).reshape(14, 16) / 256
        transposed = raw[:, 1::2].T
        self.assertFalse(transposed.flags.c_contiguous)
        result = integer_logits(transposed)
        self.assertTrue(result.flags.c_contiguous)
        for i, row in enumerate(result):
            self.assertEqual(row.strides, (2,))
            np.testing.assert_array_equal(row, np.arange(14) * 16 + i * 2 + 1)
        with self.assertRaises(ValueError): integer_logits(np.zeros((3, 1)))
        with self.assertRaises(ValueError): integer_logits(np.full((3, 14), np.nan))


if __name__ == '__main__': unittest.main()
