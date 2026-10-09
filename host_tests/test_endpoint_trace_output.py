"""Missing final frames must not look like a successful endpoint replay."""
import json
import struct
import zlib
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/voice'))
from analyze_endpoint_trace import read_replay, decode

class ReplayTests(unittest.TestCase):
    def test_exact_count_and_source_clock(self):
        raw=b'{"ms":20}\n{"ms":40}\n'
        self.assertEqual(len(read_replay(raw,2)),2)
        for wrong in (b'{"ms":20}\n',b'{"ms":20}\n{"ms":20}\n',raw+b'{"ms":60}\n'):
            with self.assertRaisesRegex(ValueError,'Incomplete or out-of-order'):read_replay(wrong,2)
    def test_truncated_json_is_not_success(self):
        with self.assertRaises(json.JSONDecodeError):read_replay(b'{"ms":20}\n{"ms":4',2)
    def test_notice_trace_integrity(self):
        frames=bytes.fromhex('0100020001a6')*3
        notices=struct.pack('<IIII',0,0,20,2049)
        events=[dict(id=1,kind='begin',frames=3,noise=100,end_ms=700),
                dict(id=1,kind='frames',offset=0,hex=frames.hex()),
                dict(id=1,kind='notices',offset=0,hex=notices.hex()),
                dict(id=1,kind='end',crc32=f'{zlib.crc32(frames):08x}',notice_count=2,
                     notice_overflow=False,notice_crc32=f'{zlib.crc32(notices):08x}')]
        def run(rows):return decode(['@endpoint '+json.dumps(e) for e in rows])
        self.assertEqual(run(events)[1]['notices'],notices)
        for patch in (dict(notice_count=3),dict(notice_overflow=True),dict(notice_crc32='00000000')):
            with self.assertRaises(ValueError):run(events[:-1]+[dict(events[-1],**patch)])
        with self.assertRaises(ValueError):run(events[:2]+events[3:])
        with self.assertRaises(ValueError):run(events[:3]+[events[2],events[3]])
        for pairs in (((20,0),(40,1)),((0,0),(20,0)),((0,0),(21,1)),((0,0),(60,1))):
            bad=b''.join(struct.pack('<II',*p) for p in pairs)
            rows=events[:2]+[dict(events[2],hex=bad.hex()),dict(events[3],notice_crc32=f'{zlib.crc32(bad):08x}')]
            with self.assertRaises(ValueError):run(rows)
        self.assertIn('end',run(events[:2]+[dict(id=1,kind='end',crc32=f'{zlib.crc32(frames):08x}')])[1])

if __name__=='__main__':unittest.main()
