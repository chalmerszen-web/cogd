"""Test binding/state contracts against independent native C export bytes."""
import ctypes
from pathlib import Path
import sys
import unittest
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'training/kws'))
from pitch_frontend import Pitch, State


class Contract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        out = ROOT/'artifacts/voice-fast/wake-pitch-feature-ux383'
        cls.library = out/'libpitch.so'
        cls.pcm = np.frombuffer((ROOT/'artifacts/voice-fast/wake-conditioned-contract-ux331/fixed.pcm').read_bytes(), dtype='<i2')
        cls.reference = np.frombuffer((ROOT/'artifacts/voice-fast/wake-pitch-board-ux382/pitch-reference.bin').read_bytes(), dtype='<i2').reshape(-1,2)

    def test_export_and_partition_state(self):
        frontend = Pitch(self.library)
        whole = frontend(self.pcm)
        np.testing.assert_array_equal(whole, self.reference)
        end_state = bytes(frontend.state)
        frontend.reset()
        cuts = [0,256,768,2048,12032,len(self.pcm)]
        partitioned = np.concatenate([frontend(self.pcm[a:b],reset=False) for a,b in zip(cuts,cuts[1:])])
        np.testing.assert_array_equal(partitioned, whole)
        self.assertEqual(bytes(frontend.state), end_state)
        np.testing.assert_array_equal(frontend(self.pcm), whole)

    def test_input_failure_is_transactional(self):
        frontend = Pitch(self.library)
        frontend(self.pcm[:1024])
        before = bytes(frontend.state)
        for invalid in (self.pcm[:257], self.pcm.reshape(-1,256), self.pcm.astype(np.float32), self.pcm.tolist()):
            with self.assertRaises(ValueError):
                frontend(invalid)
            self.assertEqual(bytes(frontend.state), before)
        self.assertEqual(frontend(np.empty(0,dtype=np.int16),reset=False).shape, (0,2))
        self.assertEqual(bytes(frontend.state), before)
        self.assertEqual(ctypes.sizeof(State),770)


if __name__ == '__main__':
    unittest.main()
