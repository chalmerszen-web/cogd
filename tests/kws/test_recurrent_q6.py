"""Separate Q6 format, independent wide-integer reference and export limits."""
import ctypes
import json
import os
from pathlib import Path
import sys
import unittest
import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from recurrent import Recurrent32, FIELDS, quantize, emit_c
from recurrent_q6 import ABI, Q6Weights, quantize_q6, emit_q6_c
from test_recurrent import Model, State, Scratch, oracle
from generate_gru_sigmoid import values


class Q6Model(ctypes.Structure):
    _fields_ = [('abi', ctypes.c_uint32), ('weights', Model)]


class Q6Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        if torch.__version__ != '2.6.0+cpu':
            raise RuntimeError('Use the pinned local PyTorch2.6 CPU environment')
        cls.lib = ctypes.CDLL(os.environ['COGD_GRU_LIB'])
        cls.lib.kws_gru32_q6_step.argtypes = [ctypes.POINTER(Q6Model), ctypes.POINTER(State),
            ctypes.POINTER(ctypes.c_int8), ctypes.POINTER(Scratch), ctypes.POINTER(ctypes.c_int16)]
        cls.lib.kws_gru32_q6_step.restype = ctypes.c_int
        cls.table = np.asarray(values(), np.int64)

    def test_independent_integer_reference(self):
        self.assertEqual(ctypes.sizeof(Q6Model), 7368)
        rng = np.random.default_rng(2026100481)
        for case in range(8):
            bundle = {n: rng.integers(-128 if t == 'int8' else -32768,
                128 if t == 'int8' else 32768, size=s, dtype=t) for n, s, t in FIELDS}
            # Multiplying matrices by4 in a separate INT64 oracle expresses
            # Q6 values in Q8 units without C's new divisors or overflow.
            wide = {n: v.astype(np.int64) * (4 if v.dtype == np.int8 else 1)
                    for n, v in bundle.items()}
            m = Q6Model(ABI, Model.from_buffer_copy(b''.join(bundle[n].tobytes() for n, _, _ in FIELDS)))
            initial = rng.integers(-32768, 32768, 32, dtype=np.int16)
            state = State((ctypes.c_int16 * 32)(*initial.tolist()))
            expected = initial.astype(np.int64)
            scratch = Scratch(); output = (ctypes.c_int16 * 2)()
            inputs = rng.integers(-128, 128, (256, 40), dtype=np.int8)
            inputs[0] = -128; inputs[1] = 127
            for row in inputs:
                expected, scores = oracle(wide, row, expected, self.table)
                self.assertEqual(self.lib.kws_gru32_q6_step(ctypes.byref(m), ctypes.byref(state),
                    row.ctypes.data_as(ctypes.POINTER(ctypes.c_int8)), ctypes.byref(scratch), output), 0)
                np.testing.assert_array_equal(np.ctypeslib.as_array(state.hidden), expected)
                np.testing.assert_array_equal(np.ctypeslib.as_array(output), scores)

    def test_invalid_format_and_alias_leave_outputs_intact(self):
        m = Q6Model(ABI, Model())
        state = State((ctypes.c_int16 * 32)(*range(32)))
        scratch = Scratch((ctypes.c_int16 * 32)(*range(32, 64)))
        output = (ctypes.c_int16 * 2)(125, -126); x = (ctypes.c_int8 * 40)()
        before = bytes(state), bytes(scratch), bytes(output)
        for tag in (0, ABI ^ 1, 0xffffffff):
            m.abi = tag
            self.assertEqual(self.lib.kws_gru32_q6_step(ctypes.byref(m), ctypes.byref(state), x,
                ctypes.byref(scratch), output), -1)
            self.assertEqual((bytes(state), bytes(scratch), bytes(output)), before)
        m.abi = ABI
        for args in (
            (None, ctypes.byref(state), x, ctypes.byref(scratch), output),
            (ctypes.byref(m), ctypes.byref(state), x,
                ctypes.cast(ctypes.byref(state), ctypes.POINTER(Scratch)), output),
            # The tag is part of the read-only model region, too.
            (ctypes.byref(m), ctypes.byref(state), x, ctypes.byref(scratch),
                ctypes.cast(ctypes.byref(m), ctypes.POINTER(ctypes.c_int16))),
            (ctypes.cast(ctypes.byref(m, 2), ctypes.POINTER(Q6Model)),
                ctypes.byref(state), x, ctypes.byref(scratch), output),
        ):
            old = bytes(m)
            self.assertEqual(self.lib.kws_gru32_q6_step(*args), -1)
            self.assertEqual((bytes(state), bytes(scratch), bytes(output)), before)
            self.assertEqual(bytes(m), old)

    def test_export_scales_range_and_format(self):
        model = Recurrent32().double().eval()
        with torch.no_grad():
            for p in model.parameters(): p.zero_()
            model.recurrent.weight_ih_l0[0, 0] = 1.25
            model.recurrent.weight_ih_l0[0, 1] = -2
            model.recurrent.weight_ih_l0[0, 2] = 127 / 64
            model.recurrent.bias_ih_l0[0] = .5
        bundle = quantize_q6(model)
        self.assertEqual(bundle.values['input'][0, 0, :3].tolist(), [80, -128, 127])
        self.assertEqual(int(bundle.values['input_bias'][0, 0]), 128)
        self.assertIn('KWS_GRU32_Q6_ABI', emit_q6_c(bundle))
        with self.assertRaises(ValueError): quantize(model)
        with self.assertRaises(ValueError): emit_c(bundle)
        with self.assertRaises(ValueError): emit_q6_c(bundle.values)
        with self.assertRaises(ValueError): emit_q6_c(Q6Weights(bundle.values, 'q8'))
        for bad in (2, -2.02, float('nan'), float('inf')):
            with torch.no_grad(): model.recurrent.weight_ih_l0[0, 0] = bad
            with self.assertRaises(ValueError): quantize_q6(model)

    def test_pytorch_reset_after_with_extended_weight_range(self):
        torch.manual_seed(2026100481)
        model = Recurrent32().double().eval()
        with torch.no_grad():
            for p in model.parameters(): p.uniform_(-.08, .08)
            model.recurrent.weight_ih_l0[64, 0] = 1.25
        bundle = quantize_q6(model).values
        parameters = (model.recurrent.weight_ih_l0, model.recurrent.weight_hh_l0,
            model.recurrent.bias_ih_l0, model.recurrent.bias_hh_l0, model.output.weight, model.output.bias)
        with torch.no_grad():
            for (n, _, t), p in zip(FIELDS, parameters):
                p.copy_(torch.from_numpy(bundle[n].astype(np.float64).reshape(p.shape) /
                    (64 if t == 'int8' else 256)))
        rng = np.random.default_rng(481)
        inputs = rng.integers(-128, 128, (128, 40), dtype=np.int8)
        with torch.no_grad(): full, states = model(torch.from_numpy(inputs.T[None].astype(np.float64) / 32))
        wide = {n: v.astype(np.int64) * (4 if v.dtype == np.int8 else 1) for n, v in bundle.items()}
        hidden = np.zeros(32, np.int64); scores = []
        for x in inputs:
            hidden, output = oracle(wide, x, hidden, self.table); scores.append(output / 256)
        np.testing.assert_allclose(np.asarray(scores).T, full.numpy()[0], atol=.02, rtol=0)
        np.testing.assert_allclose(hidden / 32768, states.numpy()[0, 0], atol=.02, rtol=0)


if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Q6Tests))
    Path(os.environ['COGD_Q6_RECEIPT']).write_text(json.dumps(dict(
        passed=result.wasSuccessful(), tests_run=result.testsRun, exact_frames=2048,
        old_format_not_reinterpreted=True, model_bytes=7368, state_bytes=64, scratch_bytes=64,
        fits=0, acoustic_quality_measured=False), indent=2), encoding='utf8')
    raise SystemExit(0 if result.wasSuccessful() else 1)
