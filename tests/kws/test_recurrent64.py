"""Independent wide-integer GRU64 oracle and old 32/Q8/Q6 compatibility."""
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
from recurrent64 import ABI, FIELDS, Recurrent64, quantize, validate, emit_c
from recurrent import Recurrent32, FIELDS as OLD_FIELDS, quantize as old_quantize
from test_recurrent import Model as OldModel, State as OldState, Scratch as OldScratch, oracle
from test_recurrent_q6 import Q6Model
from recurrent_q6 import ABI as Q6_ABI
from generate_gru_sigmoid import values


class Model(ctypes.Structure):
    _fields_ = [('abi', ctypes.c_uint32), ('input', ctypes.c_int8 * (3 * 64 * 40)),
                ('recurrent', ctypes.c_int8 * (3 * 64 * 64)),
                ('input_bias', ctypes.c_int16 * (3 * 64)),
                ('recurrent_bias', ctypes.c_int16 * (3 * 64)),
                ('output', ctypes.c_int8 * 128), ('output_bias', ctypes.c_int16 * 2)]


class State(ctypes.Structure):
    _fields_ = [('hidden', ctypes.c_int16 * 64)]


class Scratch(ctypes.Structure):
    _fields_ = [('next', ctypes.c_int16 * 64)]


def packed(bundle):
    return struct.pack('<I', ABI) + b''.join(bundle[n].tobytes() for n, _, _ in FIELDS)


class Recurrent64Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        if torch.__version__ != '2.6.0+cpu':
            raise RuntimeError('Use frozen local PyTorch2.6 CPU environment')
        cls.lib = ctypes.CDLL(os.environ['COGD_GRU_LIB'])
        cls.before = ctypes.CDLL(os.environ['COGD_GRU_BEFORE_LIB'])
        cls.lib.kws_gru64_step.argtypes = [ctypes.POINTER(Model), ctypes.POINTER(State),
            ctypes.POINTER(ctypes.c_int8), ctypes.POINTER(Scratch), ctypes.POINTER(ctypes.c_int16)]
        cls.lib.kws_gru64_step.restype = ctypes.c_int
        cls.lib.kws_gru64_reset.argtypes = [ctypes.POINTER(State)]
        cls.lib.kws_gru64_reset.restype = ctypes.c_int
        for lib in (cls.lib, cls.before):
            for name, model in (('kws_gru32_step', OldModel), ('kws_gru32_q6_step', Q6Model)):
                function = getattr(lib, name)
                function.argtypes = [ctypes.POINTER(model), ctypes.POINTER(OldState),
                    ctypes.POINTER(ctypes.c_int8), ctypes.POINTER(OldScratch), ctypes.POINTER(ctypes.c_int16)]
                function.restype = ctypes.c_int
        cls.table = np.asarray(values(), np.int64)

    def test_integer_reference_random_and_extreme(self):
        self.assertEqual((ctypes.sizeof(Model), ctypes.sizeof(State), ctypes.sizeof(Scratch)), (20872, 128, 128))
        rng = np.random.default_rng(2026100498)
        for case in range(8):
            bundle = {n: rng.integers(-128 if t == 'int8' else -32768,
                128 if t == 'int8' else 32768, size=s, dtype=t) for n, s, t in FIELDS}
            initial = rng.integers(-32768, 32768, 64, dtype=np.int16)
            inputs = rng.integers(-128, 128, (256, 40), dtype=np.int8)
            inputs[:2] = np.array([-128, 127], dtype=np.int8)[:, None]
            if case >= 4:
                # Both signs and the worst recurrent product, with all 64
                # previous states extreme. This also tests saturating logits.
                for n, _, t in FIELDS:
                    bundle[n].fill((-128 if t == 'int8' else -32768) if case & 1
                                   else (127 if t == 'int8' else 32767))
                initial.fill(-32768 if case & 2 else 32767)
            validate(bundle)
            model = Model.from_buffer_copy(packed(bundle))
            state = State((ctypes.c_int16 * 64)(*initial.tolist()))
            expected = initial.astype(np.int64)
            scratch = Scratch(); output = (ctypes.c_int16 * 2)()
            for row in inputs:
                expected, scores = oracle(bundle, row, expected, self.table)
                self.assertEqual(self.lib.kws_gru64_step(ctypes.byref(model), ctypes.byref(state),
                    row.ctypes.data_as(ctypes.POINTER(ctypes.c_int8)), ctypes.byref(scratch), output), 0)
                np.testing.assert_array_equal(np.ctypeslib.as_array(state.hidden), expected)
                np.testing.assert_array_equal(np.ctypeslib.as_array(output), scores)

    def test_old_both_formats_exact_frozen_before(self):
        rng = np.random.default_rng(202610498)
        for symbol, cls in (('kws_gru32_step', OldModel), ('kws_gru32_q6_step', Q6Model)):
            for case in range(4):
                bundle = {n: rng.integers(-128 if t == 'int8' else -32768,
                    128 if t == 'int8' else 32768, size=s, dtype=t) for n, s, t in OLD_FIELDS}
                payload = b''.join(bundle[n].tobytes() for n, _, _ in OLD_FIELDS)
                model = cls.from_buffer_copy((struct.pack('<I', Q6_ABI) if cls is Q6Model else b'') + payload)
                initial = rng.integers(-32768, 32768, 32, dtype=np.int16)
                states = [OldState((ctypes.c_int16 * 32)(*initial.tolist())) for _ in range(2)]
                scratch = [OldScratch(), OldScratch()]
                output = [(ctypes.c_int16 * 2)(), (ctypes.c_int16 * 2)()]
                for row in rng.integers(-128, 128, (256, 40), dtype=np.int8):
                    for i, lib in enumerate((self.before, self.lib)):
                        self.assertEqual(getattr(lib, symbol)(ctypes.byref(model), ctypes.byref(states[i]),
                            row.ctypes.data_as(ctypes.POINTER(ctypes.c_int8)), ctypes.byref(scratch[i]), output[i]), 0)
                    self.assertEqual((bytes(states[0]), bytes(scratch[0]), bytes(output[0])),
                                     (bytes(states[1]), bytes(scratch[1]), bytes(output[1])))

    def test_streaming_mapping_and_export(self):
        torch.manual_seed(2026100498)
        model = Recurrent64().double().eval()
        self.assertEqual(sum(p.numel() for p in model.parameters()), 20482)
        x = torch.randn(2, 40, 160, dtype=torch.float64)
        with torch.no_grad():
            full, final = model(x)
            state = None; parts = []
            for chunk in x.split([1, 17, 33, 109], dim=-1):
                output, state = model(chunk, state); parts.append(output)
        torch.testing.assert_close(torch.cat(parts, dim=-1), full, atol=1e-12, rtol=1e-12)
        torch.testing.assert_close(state, final, atol=1e-12, rtol=1e-12)
        bundle = quantize(model)
        self.assertEqual(len(packed(bundle)), 20872)
        out = Path(os.environ['COGD_GRU64_TEST_OUT'])
        (out / 'export-source.c').write_text(emit_c(bundle, 'contract_export'), encoding='utf8')
        (out / 'export-model.bin').write_bytes(packed(bundle))
        with self.assertRaises(ValueError): old_quantize(model)
        with self.assertRaises(ValueError): quantize(Recurrent32().eval())
        model.train()
        with self.assertRaises(ValueError): quantize(model)
        output, _ = model(x)
        output.square().mean().backward()
        self.assertTrue(all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.parameters()))

    def test_float_reset_after_quantized_error(self):
        torch.manual_seed(498)
        model = Recurrent64().double().eval()
        with torch.no_grad():
            for parameter in model.parameters(): parameter.uniform_(-.025, .025)
        bundle = quantize(model)
        with torch.no_grad():
            parameters = (model.recurrent.weight_ih_l0, model.recurrent.weight_hh_l0,
                model.recurrent.bias_ih_l0, model.recurrent.bias_hh_l0, model.output.weight, model.output.bias)
            for (n, _, _), p in zip(FIELDS, parameters):
                p.copy_(torch.from_numpy(bundle[n].astype(np.float64).reshape(p.shape) / 256))
        rng = np.random.default_rng(498)
        expected = np.zeros(64, np.int64); state = None; errors = []
        with torch.no_grad():
            for row in rng.integers(-128, 128, (128, 40), dtype=np.int8):
                expected, _ = oracle(bundle, row, expected, self.table)
                _, state = model(torch.from_numpy(row.astype(np.float64) / 32)[None, :, None], state)
                errors.append(float(np.max(np.abs(expected / 32768 - state[0, 0].numpy()))))
            self.assertLess(max(errors), .02)
            for p in model.parameters(): p.zero_()
            model.recurrent.bias_ih_l0[128:] = -2
            model.recurrent.bias_hh_l0[128:] = 4
            _, state = model(torch.zeros(1, 40, 1, dtype=torch.float64))
        torch.testing.assert_close(state, torch.zeros_like(state), atol=0, rtol=0)

    def test_format_range_and_input_guards(self):
        model = Recurrent64().eval()
        bundle = quantize(model)
        with self.assertRaises(ValueError): emit_c(bundle, 'bad;symbol')
        with self.assertRaises(ValueError): validate({})
        for n, s, t in FIELDS:
            broken = dict(bundle); broken[n] = np.zeros(s, dtype=np.float32)
            with self.assertRaises(ValueError): validate(broken)
        for bad in (.5, -.503, float('nan'), float('inf')):
            with torch.no_grad(): model.output.weight[0, 0] = bad
            with self.assertRaises(ValueError): quantize(model)
        for x in (torch.zeros(1, 39, 1), torch.zeros(0, 40, 1), torch.zeros(1, 40, 0),
                  torch.zeros(1, 40, 1, dtype=torch.int8)):
            with self.assertRaises(ValueError): model(x)


if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Recurrent64Tests))
    raise SystemExit(0 if result.wasSuccessful() else 1)
