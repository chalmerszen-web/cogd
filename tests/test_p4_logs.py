"""USB export must reject torn or mismatched pages, and retain boot/time identity."""
import importlib.util
from pathlib import Path
import struct
import sys
import unittest
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
spec=importlib.util.spec_from_file_location('p4_logs',ROOT/'tools/p4_logs.py')
logs=importlib.util.module_from_spec(spec);spec.loader.exec_module(logs)

class JournalExport(unittest.TestCase):
    def page(self):
        data=bytearray(b'\xff'*4096);text='重启前的语音错误'.encode()
        struct.pack_into('<IHHQIIQQIIIIII',data,0,0x31474c44,1,64,42,0x1234,76+len(text),
                         5000,1794300005000,3,2,5,0,0,0)
        struct.pack_into('<QHBB',data,64,4900,len(text),2,0);data[76:76+len(text)]=text
        struct.pack_into('<I',data,60,zlib.crc32(data[64:4092],zlib.crc32(data[:60])))
        struct.pack_into('<I',data,4092,0x43474c44)
        return data
    def test_identity_and_clock(self):
        meta,rows=logs.decode_page(self.page(),42)
        self.assertEqual(meta['reset_name'],'interrupt_watchdog')
        self.assertEqual(meta['dropped'],3)
        self.assertEqual(rows[0]['text'],'重启前的语音错误')
        self.assertEqual(rows[0]['unix_ms'],1794300004900)
        self.assertEqual(rows[0]['boot'],0x1234)
    def test_wrong_sequence(self):
        with self.assertRaises(ValueError):logs.decode_page(self.page(),43)
    def test_torn_commit(self):
        data=self.page();data[-1]=255
        with self.assertRaises(ValueError):logs.decode_page(data)
    def test_corruption(self):
        data=self.page();data[90]^=1
        with self.assertRaises(ValueError):logs.decode_page(data)

if __name__=='__main__':unittest.main()
