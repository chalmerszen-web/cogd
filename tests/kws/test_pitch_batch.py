"""Batch ABI equivalence to old logmel and independent C pitch references."""
import json
from pathlib import Path
import sys
import unittest
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'training/kws'))
from pitch_batch import BatchFrontend
from data import Frontend


class Contract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.batch=BatchFrontend(ROOT/'artifacts/voice-fast/wake-pitch-batch-ux387/libbatch.so')
        folder=ROOT/'artifacts/voice-fast/wake-conditioned-contract-ux331'
        cls.pcm=np.frombuffer((folder/'fixed.pcm').read_bytes(),dtype='<i2')
        cls.logmel=np.array([r['logmel'] for r in json.loads((folder/'fixed-traces.json').read_text())],dtype=np.int16)
        cls.pitch=np.frombuffer((ROOT/'artifacts/voice-fast/wake-pitch-board-ux384/pitch-reference.bin').read_bytes(),dtype='<i2').reshape(-1,2)

    def test_fixed_independent_references_and_reset(self):
        logmel,pitch=self.batch(self.pcm)
        np.testing.assert_array_equal(logmel,self.logmel)
        np.testing.assert_array_equal(pitch,self.pitch)
        other=self.pcm[::-1].copy()
        np.testing.assert_array_equal(self.batch(other)[0],Frontend()(other))
        again=self.batch(self.pcm)
        np.testing.assert_array_equal(again[0],logmel)
        np.testing.assert_array_equal(again[1],pitch)

    def test_invalid_arguments(self):
        for invalid in (self.pcm[:257],self.pcm.astype(np.float32),self.pcm.reshape(-1,256),np.empty(0,dtype=np.int16)):
            with self.assertRaises(ValueError):self.batch(invalid)


if __name__=='__main__':unittest.main()
