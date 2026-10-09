"""Host rejects corrupt framing instead of assigning it to an audio recording."""
import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

spec=importlib.util.spec_from_file_location('vad_probe',Path(__file__).resolve().parents[1]/'tools/voice/replay_vendor_vad.py')
probe=importlib.util.module_from_spec(spec);spec.loader.exec_module(probe)

class ReplayTest(unittest.TestCase):
    def test_mode_identity(self):
        self.assertEqual(probe.check_probe(dict(version=probe.VERSION),{}),3)
        declared=dict(firmware='0.11.139-vad-replay-mode2',vad_mode=2)
        self.assertEqual(probe.check_probe(dict(version=declared['firmware'],vad_mode=2),declared),2)
        for state in (dict(version=probe.VERSION),dict(version=declared['firmware']),
                      dict(version=declared['firmware'],vad_mode=3),
                      dict(version=declared['firmware'],vad_mode=True)):
            with self.assertRaises(ValueError):probe.check_probe(state,declared)
        for mode in (-1,0,4,True,'2'):
            with self.assertRaises(ValueError):probe.check_probe({},dict(vad_mode=mode))
    def test_wire_preserves_signed_extremes(self):
        pcm=struct.pack('<320h',*([-32768,32767,0,1,-1]*64))
        command=probe.frame_command(499,pcm).split()
        self.assertEqual(command[:2],['frame','499'])
        self.assertEqual(bytes.fromhex(command[2]),pcm)
        self.assertEqual(int(command[3],16),zlib.crc32(pcm))
    def test_filter_identity_and_energy_parity(self):
        declared=dict(firmware='0.11.148-vad-replay-clean',vad_mode=2,input_filter='clean')
        state=dict(version=declared['firmware'],vad_mode=2,input_filter='clean')
        self.assertEqual(probe.check_probe(state,declared),2)
        for filtering in (None,'production','unknown',True):
            bad=dict(state,input_filter=filtering)
            with self.assertRaises(ValueError):probe.check_probe(bad,declared)
        with self.assertRaises(ValueError):probe.check_probe(state,dict(declared,input_filter='production'))
        a=struct.pack('<HHBB',128,240,0,0xa6)
        b=struct.pack('<HHBB',128,240,1,0xa6)
        self.assertTrue(probe.same_energy(a,b))
        for bad in (b'',b+b,struct.pack('<HHBB',129,240,1,0xa6),
                    struct.pack('<HHBB',128,241,1,0xa6),a[:-2]+b'\x02\xa6',a[:-1]+b'\0'):
            self.assertFalse(probe.same_energy(a,bad))
    def test_bounds(self):
        for index in (-1,500,True):
            with self.assertRaises(ValueError):probe.frame_command(index,bytes(640))
        for length in (0,639,641):
            with self.assertRaises(ValueError):probe.frame_command(0,bytes(length))
    def test_order(self):
        for value in (-1,1,True):
            with self.assertRaises(ValueError):probe.metadata(dict(frame=value,level=0,clean=0,spectral=False),0)
    def test_metadata(self):
        self.assertEqual(probe.metadata(dict(frame=7,level=32768,clean=258,spectral=True),7),b'\0\x80\x02\x01\x01\xa6')
        for key,value in [('spectral',1),('level',True),('clean',-1),('level',32769),('frame','7')]:
            row=dict(frame=7,level=0,clean=0,spectral=False);row[key]=value
            with self.assertRaises(ValueError):probe.metadata(row,7)

if __name__=='__main__':unittest.main()
