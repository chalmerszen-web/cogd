"""Independent integer oracle, gate convention and host training ABI checks."""
import ctypes
import hashlib
import inspect
import json
import os
from pathlib import Path
import sys
import unittest
import numpy as np
import torch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
sys.path.insert(0,str(ROOT/'tools/kws'))
from recurrent import Recurrent32,FIELDS,quantize,validate,emit_c
from generate_gru_sigmoid import values


class Model(ctypes.Structure):
    _fields_=[('input',ctypes.c_int8*(3*32*40)),('recurrent',ctypes.c_int8*(3*32*32)),
              ('input_bias',ctypes.c_int16*(3*32)),('recurrent_bias',ctypes.c_int16*(3*32)),
              ('output',ctypes.c_int8*(2*32)),('output_bias',ctypes.c_int16*2)]


class State(ctypes.Structure):
    _fields_=[('hidden',ctypes.c_int16*32)]


class Scratch(ctypes.Structure):
    _fields_=[('next',ctypes.c_int16*32)]


def nearest(value,divisor):
    value=np.asarray(value,dtype=np.int64)
    return np.sign(value)*((np.abs(value)+divisor//2)//divisor)


def oracle(bundle,input,state,table):
    # Independent wide matrix contraction, never calls the C gate helpers.
    x=nearest(np.einsum('ghi,i->gh',bundle['input'].astype(np.int64),input),32)+bundle['input_bias']
    h=nearest(np.einsum('ghi,i->gh',bundle['recurrent'].astype(np.int64),state),32768)+bundle['recurrent_bias']
    def sigmoid(q8):
        positions=np.clip(q8,-4096,4096)+4096
        slot,part=np.divmod(positions,16)
        return (table[slot]*(16-part)+table[np.minimum(slot+1,512)]*part+8)//16
    r,z=sigmoid(x[:2]+h[:2])
    n=np.clip(2*sigmoid(2*(x[2]+nearest(r*h[2],32768)))-32768,-32768,32767)
    state=np.clip(nearest((32768-z)*n+z*state,32768),-32768,32767)
    logits=np.clip(nearest(bundle['output'].astype(np.int64)@state,32768)+bundle['output_bias'],-32768,32767)
    return state,logits


class RecurrentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        if torch.__version__!='2.6.0+cpu':
            raise RuntimeError('Use the frozen local PyTorch2.6 CPU environment')
        cls.library=ctypes.CDLL(os.environ['COGD_GRU_LIB'])
        cls.library.kws_gru32_step.argtypes=[ctypes.POINTER(Model),ctypes.POINTER(State),
            ctypes.POINTER(ctypes.c_int8),ctypes.POINTER(Scratch),ctypes.POINTER(ctypes.c_int16)]
        cls.library.kws_gru32_step.restype=ctypes.c_int
        cls.library.kws_gru32_reset.argtypes=[ctypes.POINTER(State)]
        cls.library.kws_gru32_reset.restype=ctypes.c_int
        cls.table=np.asarray(values(),np.int64)

    def test_integer_states_and_outputs(self):
        self.assertEqual((ctypes.sizeof(Model),ctypes.sizeof(State),ctypes.sizeof(Scratch)),(7364,64,64))
        rng=np.random.default_rng(2026100475)
        frames=0
        for model_index in range(4):
            bundle={name:rng.integers(-128 if dtype=='int8' else -32768,
                128 if dtype=='int8' else 32768,size=shape,dtype=dtype) for name,shape,dtype in FIELDS}
            validate(bundle)
            model=Model.from_buffer_copy(b''.join(bundle[name].tobytes() for name,_,_ in FIELDS))
            initial=rng.integers(-32768,32768,32,dtype=np.int16)
            state=State((ctypes.c_int16*32)(*initial.tolist()))
            scratch=Scratch(); output=(ctypes.c_int16*2)()
            expected=initial.astype(np.int64)
            chunks=[1,17,32,93,113]
            inputs=rng.integers(-128,128,size=(256,40),dtype=np.int8)
            offset=0
            for chunk in chunks:
                for row in inputs[offset:offset+chunk]:
                    expected,scores=oracle(bundle,row,expected,self.table)
                    result=self.library.kws_gru32_step(ctypes.byref(model),ctypes.byref(state),
                        row.ctypes.data_as(ctypes.POINTER(ctypes.c_int8)),ctypes.byref(scratch),output)
                    self.assertEqual(result,0)
                    np.testing.assert_array_equal(np.ctypeslib.as_array(state.hidden),expected)
                    np.testing.assert_array_equal(np.ctypeslib.as_array(output),scores)
                    frames+=1
                offset+=chunk
            self.assertEqual(offset,256)
        self.assertEqual(frames,1024)

    def test_streaming_host_and_parameter_mapping(self):
        torch.manual_seed(2026100475)
        model=Recurrent32().double().eval()
        self.assertEqual(sum(parameter.numel() for parameter in model.parameters()),7170)
        inputs=torch.randn(2,40,160,dtype=torch.float64)
        with torch.no_grad():
            full,expected_state=model(inputs)
            state=None;parts=[]
            for chunk in inputs.split([1,17,33,109],dim=-1):
                output,state=model(chunk,state);parts.append(output)
        torch.testing.assert_close(torch.cat(parts,dim=-1),full,atol=1e-12,rtol=1e-12)
        torch.testing.assert_close(state,expected_state,atol=1e-12,rtol=1e-12)
        bundle=quantize(model)
        self.assertEqual(sum(value.nbytes for value in bundle.values()),7364)
        source=emit_c(bundle)
        self.assertIn('.recurrent_bias=',source)
        with self.assertRaises(ValueError):emit_c(bundle,'invalid;symbol')
        with self.assertRaises(ValueError):model(torch.zeros(1,39,1))
        model.train()
        with self.assertRaises(ValueError):quantize(model)
        # Every training tensor participates in the recurrent output path.
        output,_=model(inputs)
        output.square().mean().backward()
        self.assertTrue(all(parameter.grad is not None and torch.isfinite(parameter.grad).all()
                            for parameter in model.parameters()))

    def test_reset_after_convention_and_integer_error(self):
        torch.manual_seed(47)
        model=Recurrent32().double().eval()
        with torch.no_grad():
            for parameter in model.parameters():parameter.uniform_(-.025,.025)
        bundle=quantize(model)
        # Load the dequantized artifact into standard PyTorch GRU for a separate
        # float gate-convention check. This is not exact integer equivalence.
        with torch.no_grad():
            parameters=(model.recurrent.weight_ih_l0,model.recurrent.weight_hh_l0,
                model.recurrent.bias_ih_l0,model.recurrent.bias_hh_l0,model.output.weight,model.output.bias)
            for (name,_,_),parameter in zip(FIELDS,parameters):
                parameter.copy_(torch.from_numpy(bundle[name].astype(np.float64).reshape(parameter.shape)/256))
        rng=np.random.default_rng(475)
        inputs=rng.integers(-128,128,size=(128,40),dtype=np.int8)
        state=None;expected=np.zeros(32,np.int64);errors=[]
        with torch.no_grad():
            for row in inputs:
                expected,_=oracle(bundle,row,expected,self.table)
                _,state=model(torch.from_numpy(row.astype(np.float64)/32)[None,:,None],state)
                errors.append(float(np.max(np.abs(expected/32768-state[0,0].numpy()))))
        self.assertLess(max(errors),.02)
        # Reset-after must multiply the recurrent n bias by r.
        with torch.no_grad():
            for parameter in model.parameters():parameter.zero_()
            model.recurrent.bias_ih_l0[64:]=-2
            model.recurrent.bias_hh_l0[64:]=4
            _,state=model(torch.zeros(1,40,1,dtype=torch.float64))
        torch.testing.assert_close(state,torch.zeros_like(state),atol=0,rtol=0)

    def test_quantized_boundaries(self):
        model=Recurrent32().eval()
        with torch.no_grad():model.output.weight[0,0]=.5
        with self.assertRaises(ValueError):quantize(model)
        with torch.no_grad():model.output.weight[0,0]=float('nan')
        with self.assertRaises(ValueError):quantize(model)
        with self.assertRaises(ValueError):validate({})


if __name__=='__main__':
    runner=unittest.TextTestRunner(verbosity=2)
    result=runner.run(unittest.defaultTestLoader.loadTestsFromTestCase(RecurrentTests))
    manifest=dict(complete=result.wasSuccessful(),tests=result.testsRun,torch_version=torch.__version__,
        torch_rnn_sha256=hashlib.sha256(Path(inspect.getsourcefile(torch.nn.GRU)).read_bytes()).hexdigest(),
        Python_integer_frames=1024,Python_integer_states=32768,
        C_library_sha256=hashlib.sha256(Path(os.environ['COGD_GRU_LIB']).read_bytes()).hexdigest(),
        trained=False,quality_not_tested=True,goal_complete=False)
    Path(os.environ['COGD_GRU_RECEIPT']).write_text(json.dumps(manifest,indent=2),encoding='utf8')
    raise SystemExit(0 if result.wasSuccessful() else 1)
