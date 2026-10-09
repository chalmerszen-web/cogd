"""A fixed8s diagnostic timeout must reproduce the board's forced LIMIT."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

REPLAY=Path(sys.argv.pop(1)).resolve()

class ShadowTests(unittest.TestCase):
    def replay(self, voiced, total=400, shadow=True):
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory)
            (base/'frames').write_bytes(struct.pack('<HHBB',1000,1000,1,0xa6)*voiced+
                                       struct.pack('<HHBB',0,0,0,0xa6)*(total-voiced))
            (base/'notices').write_bytes(struct.pack('<II',0,0))
            result=subprocess.run([str(REPLAY),str(base/'frames'),'100','700','--supported',
                '--notices',str(base/'notices')]+(['--shadow'] if shadow else []),capture_output=True)
            if result.returncode:return result.returncode,[]
            return 0,[json.loads(row) for row in result.stdout.splitlines()]

    def test_only_shadow_forces_eight_second_limit(self):
        error,rows=self.replay(400);self.assertEqual(error,0)
        self.assertEqual(len(rows),400)
        self.assertEqual((rows[-2]['state'],rows[-1]['state'],rows[-1]['elapsed_ms']),(1,4,8000))
        error,normal=self.replay(400,shadow=False);self.assertEqual(error,0)
        self.assertEqual(normal[-1]['state'],1)

    def test_prior_terminal_is_immutable(self):
        for voiced,state,ms in ((0,3,4000),(100,2,2700)):
            error,rows=self.replay(voiced);self.assertEqual(error,0)
            terminal=next(row for row in rows if row['state']>=2)
            self.assertEqual((terminal['state'],terminal['elapsed_ms']),(state,ms))
            self.assertEqual((rows[-1]['state'],rows[-1]['elapsed_ms']),(state,ms))

    def test_incomplete_shadow_data_is_not_complete(self):
        error,rows=self.replay(300,total=300);self.assertNotEqual(error,0);self.assertEqual(rows,[])

if __name__=='__main__':unittest.main()
