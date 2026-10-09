"""Budget/checksum checks only: no serial, flash, or external process access."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/kws'))
from flash_guard import app_limit,validate_application

def image(size,label=True):
    assert size%16==0 and size>=96
    blob=bytearray(size)
    blob[0]=0xe9;blob[1]=1;blob[23]=1
    struct.pack_into('<II',blob,24,0x3c020020,size-80)
    marker=b'-protocol-diag' if label is True else label if isinstance(label,bytes) else b''
    blob[32:32+len(marker)]=marker
    checksum=0xef
    for byte in blob[32:size-48]:checksum^=byte
    blob[size-33]=checksum
    blob[-32:]=hashlib.sha256(blob[:-32]).digest()
    return blob

class Limits(unittest.TestCase):
    def test_normal_reserve(self):
        validate_application(image(app_limit(),False))
        with self.assertRaises(ValueError):validate_application(image(app_limit()+16))
    def test_diagnostic_is_bounded_and_labelled(self):
        self.assertLess(app_limit(True),0x180000)
        validate_application(image(app_limit(True)),True)
        with self.assertRaises(ValueError):validate_application(image(app_limit(True)+16),True)
        with self.assertRaises(ValueError):validate_application(image(app_limit(),False),True)
        with self.assertRaises(ValueError):validate_application(image(0x180000),True)
    def test_integrity_still_required(self):
        damaged=image(96);damaged[-1]^=1
        with self.assertRaises(ValueError):validate_application(damaged,True)
        with self.assertRaises(ValueError):validate_application(image(96)+b'padding',True)
    def test_cross_resource_budget_stays_in_existing_slot(self):
        limit=app_limit(kws_cross_diagnostic=True)
        self.assertEqual(limit,1540096+8192)
        self.assertLess(limit,0x180000)
        validate_application(image(limit,b'-cross-budget'),kws_cross_diagnostic=True)
        with self.assertRaises(ValueError):validate_application(image(limit+16,b'-cross-budget'),kws_cross_diagnostic=True)
        with self.assertRaises(ValueError):validate_application(image(limit,b'-cross-budget'))
        with self.assertRaises(ValueError):validate_application(image(0x180000,b'-cross-budget'),kws_cross_diagnostic=True)
    def test_diagnostic_labels_and_modes_do_not_cross(self):
        with self.assertRaises(ValueError):validate_application(image(96),kws_cross_diagnostic=True)
        with self.assertRaises(ValueError):validate_application(image(96,b'-cross-budget'),protocol_diagnostic=True)
        with self.assertRaises(ValueError):validate_application(image(96,False),kws_cross_diagnostic=True)
        with self.assertRaises(ValueError):app_limit(True,True)
        with self.assertRaises(ValueError):validate_application(image(96),True,True)
    def test_cross_integrity_is_required(self):
        damaged=image(96,b'-cross-budget');damaged[-1]^=1
        with self.assertRaises(ValueError):validate_application(damaged,kws_cross_diagnostic=True)
        with self.assertRaises(ValueError):validate_application(image(96,b'-cross-budget')+b'padding',kws_cross_diagnostic=True)

if __name__=='__main__':unittest.main()
