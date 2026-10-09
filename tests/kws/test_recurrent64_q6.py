"""Wide integer reference, tagged64/Q6 limits and frozen64/Q8 regression."""
import ctypes
import os
from pathlib import Path
import struct
import sys
import unittest
import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'training/kws'))
from recurrent64 import Recurrent64, FIELDS, ABI as Q8_ABI, quantize, emit_c
from recurrent64_q6 import ABI, Q6Weights, quantize_q6, emit_q6_c
from recurrent_q6 import Q6Weights as OldQ6Weights
from recurrent import Recurrent32
from test_recurrent import oracle
from test_recurrent64 import Model, State, Scratch
from generate_gru_sigmoid import values


class Q6Model(Model):
    pass


def packed(bundle):
    return struct.pack('<I', ABI) + b''.join(bundle[n].tobytes() for n, _, _ in FIELDS)


class Q6Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        if torch.__version__ != '2.6.0+cpu':
            raise RuntimeError('Frozen Torch2.6 CPU required')
        cls.lib = ctypes.CDLL(os.environ['COGD_GRU_LIB'])
        cls.before = ctypes.CDLL(os.environ['COGD_GRU_BEFORE_LIB'])
        cls.lib.kws_gru64_q6_step.argtypes = [ctypes.POINTER(Q6Model), ctypes.POINTER(State),
            ctypes.POINTER(ctypes.c_int8), ctypes.POINTER(Scratch), ctypes.POINTER(ctypes.c_int16)]
        cls.lib.kws_gru64_q6_step.restype = ctypes.c_int
        for lib in (cls.lib, cls.before):
            lib.kws_gru64_step.argtypes = [ctypes.POINTER(Model), ctypes.POINTER(State),
                ctypes.POINTER(ctypes.c_int8), ctypes.POINTER(Scratch), ctypes.POINTER(ctypes.c_int16)]
            lib.kws_gru64_step.restype = ctypes.c_int
        cls.table = np.asarray(values(), np.int64)

    def test_independent_wide_reference_and_tight_products(self):
        self.assertEqual(ctypes.sizeof(Q6Model), 20872)
        rng = np.random.default_rng(2026100503)
        for case in range(10):
            bundle = {n: rng.integers(-128 if t == 'int8' else -32768,
                128 if t == 'int8' else 32768, size=s, dtype=t) for n, s, t in FIELDS}
            initial = rng.integers(-32768, 32768, 64, dtype=np.int16)
            inputs = rng.integers(-128, 128, (256, 40), dtype=np.int8)
            inputs[0] = -128; inputs[1] = 127
            if case >= 8:
                for n, _, t in FIELDS:
                    bundle[n].fill(-128 if t == 'int8' else 32767)
                bundle['recurrent_bias'].fill(32767 if case == 8 else -32768)
                initial.fill(-32768 if case == 8 else 32767)
                inputs.fill(-128)
            wide = {n: v.astype(np.int64) * (4 if v.dtype == np.int8 else 1) for n, v in bundle.items()}
            model = Q6Model.from_buffer_copy(packed(bundle))
            state = State((ctypes.c_int16 * 64)(*initial.tolist()))
            expected = initial.astype(np.int64)
            scratch = Scratch(); output = (ctypes.c_int16 * 2)()
            for row in inputs:
                expected, scores = oracle(wide, row, expected, self.table)
                self.assertEqual(self.lib.kws_gru64_q6_step(ctypes.byref(model), ctypes.byref(state),
                    row.ctypes.data_as(ctypes.POINTER(ctypes.c_int8)), ctypes.byref(scratch), output), 0)
                np.testing.assert_array_equal(np.ctypeslib.as_array(state.hidden), expected)
                np.testing.assert_array_equal(np.ctypeslib.as_array(output), scores)

    def test_old64_q8_frozen_exact(self):
        rng = np.random.default_rng(202610503)
        for case in range(4):
            bundle = {n: rng.integers(-128 if t == 'int8' else -32768,
                128 if t == 'int8' else 32768, size=s, dtype=t) for n, s, t in FIELDS}
            model = Model.from_buffer_copy(struct.pack('<I', Q8_ABI) + packed(bundle)[4:])
            initial = rng.integers(-32768, 32768, 64, dtype=np.int16)
            states = [State((ctypes.c_int16 * 64)(*initial.tolist())) for _ in range(2)]
            scratch = [Scratch(), Scratch()]; output = [(ctypes.c_int16 * 2)() for _ in range(2)]
            for row in rng.integers(-128, 128, (256, 40), dtype=np.int8):
                for i, lib in enumerate((self.before, self.lib)):
                    self.assertEqual(lib.kws_gru64_step(ctypes.byref(model), ctypes.byref(states[i]),
                        row.ctypes.data_as(ctypes.POINTER(ctypes.c_int8)), ctypes.byref(scratch[i]), output[i]), 0)
                self.assertEqual((bytes(states[0]), bytes(scratch[0]), bytes(output[0])),
                                 (bytes(states[1]), bytes(scratch[1]), bytes(output[1])))

    def test_cross_format_rejection_before_any_write(self):
        model = Q6Model(); x = (ctypes.c_int8 * 40)()
        state = State((ctypes.c_int16 * 64)(*range(64)))
        scratch = Scratch((ctypes.c_int16 * 64)(*range(64, 128)))
        output = (ctypes.c_int16 * 2)(123, -321)
        before = bytes(state), bytes(scratch), bytes(output)
        for tag in (0, Q8_ABI, 0x3667524b, ABI ^ 1, 0xffffffff):
            model.abi = tag
            original = bytes(model)
            self.assertEqual(self.lib.kws_gru64_q6_step(ctypes.byref(model), ctypes.byref(state), x,
                ctypes.byref(scratch), output), -1)
            self.assertEqual((bytes(state), bytes(scratch), bytes(output)), before)
            self.assertEqual(bytes(model), original)
        model.abi = ABI
        self.assertEqual(self.lib.kws_gru64_step(ctypes.cast(ctypes.byref(model), ctypes.POINTER(Model)),
            ctypes.byref(state), x, ctypes.byref(scratch), output), -1)
        self.assertEqual((bytes(state), bytes(scratch), bytes(output)), before)

    def test_export_scales_tags_limits(self):
        model = Recurrent64().double().eval()
        with torch.no_grad():
            for p in model.parameters(): p.zero_()
            model.recurrent.weight_ih_l0[0, :3] = torch.tensor([1.25, -2, 127 / 64])
            model.recurrent.bias_ih_l0[0] = .5
        bundle = quantize_q6(model)
        self.assertEqual(bundle.values['input'][0, 0, :3].tolist(), [80, -128, 127])
        self.assertEqual(int(bundle.values['input_bias'][0, 0]), 128)
        out = Path(os.environ['COGD_GRU64_TEST_OUT'])
        (out / 'q6-export-source.c').write_text(emit_q6_c(bundle, 'contract_q6_export'), encoding='utf8')
        (out / 'q6-export-model.bin').write_bytes(packed(bundle.values))
        with self.assertRaises(ValueError): quantize(model)
        with self.assertRaises(ValueError): emit_c(bundle)
        for wrong in (bundle.values, Q6Weights(bundle.values, 'q8'), OldQ6Weights(bundle.values)):
            with self.assertRaises(ValueError): emit_q6_c(wrong)
        with self.assertRaises(ValueError): quantize_q6(Recurrent32().eval())
        for bad in (2, -2.02, float('nan'), float('inf')):
            with torch.no_grad(): model.recurrent.weight_ih_l0[0, 0] = bad
            with self.assertRaises(ValueError): quantize_q6(model)
        model.train()
        with self.assertRaises(ValueError): quantize_q6(model)

    def test_pytorch_gate_convention_extended_range(self):
        torch.manual_seed(2026100503)
        model = Recurrent64().double().eval()
        with torch.no_grad():
            for p in model.parameters(): p.uniform_(-.08, .08)
            model.recurrent.weight_ih_l0[128, 0] = 1.25
        bundle = quantize_q6(model).values
        parameters = (model.recurrent.weight_ih_l0, model.recurrent.weight_hh_l0,
            model.recurrent.bias_ih_l0, model.recurrent.bias_hh_l0, model.output.weight, model.output.bias)
        with torch.no_grad():
            for (n, _, t), p in zip(FIELDS, parameters):
                p.copy_(torch.from_numpy(bundle[n].astype(np.float64).reshape(p.shape) / (64 if t == 'int8' else 256)))
        inputs = np.random.default_rng(503).integers(-128, 128, (128, 40), dtype=np.int8)
        with torch.no_grad(): full, states = model(torch.from_numpy(inputs.T[None].astype(np.float64) / 32))
        wide = {n: v.astype(np.int64) * (4 if v.dtype == np.int8 else 1) for n, v in bundle.items()}
        hidden = np.zeros(64, np.int64); scores = []
        for x in inputs:
            hidden, output = oracle(wide, x, hidden, self.table); scores.append(output / 256)
        np.testing.assert_allclose(np.asarray(scores).T, full.numpy()[0], atol=.02, rtol=0)
        np.testing.assert_allclose(hidden / 32768, states.numpy()[0, 0], atol=.02, rtol=0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
