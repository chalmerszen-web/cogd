import io
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/voice'))
from device import Device

class VoiceEventParsing(unittest.TestCase):
    def parse(self,raw):
        d=Device.__new__(Device)
        d.link=io.BytesIO(raw);d.raw=io.BytesIO();d.buffer=b'';d.unread=[];d.events=[]
        d.lines()
        self.assertEqual(d.raw.getvalue(),raw)
        return d.events

    def test_complete_event_with_observed_ns_suffix(self):
        rows=self.parse(b'@voice {"stage":"realtime_test_empty_final","time_ms":273376,"text":"item_RdZm"}WebRtcNs_Create: self=0x489eb3d0 size:23632\r\n')
        self.assertEqual(len(rows),1)
        self.assertEqual(rows[0]['time_ms'],273376)
        self.assertEqual(rows[0]['text'],'item_RdZm')
        self.assertIn('observed',rows[0])

    def test_split_json_is_not_reconstructed(self):
        self.assertEqual(self.parse(b'@voice {"stage":"realtime_asr_retry","heap_time_WebRtcNs_Create: self=0x487c07c0 size:23632\r\nms":47406}\r\n'),[])

    def test_unknown_suffix_and_multiple_objects_rejected(self):
        for suffix in (b'rubbish',b'{"stage":"other"}'):
            self.assertEqual(self.parse(b'@voice {"stage":"ready"}'+suffix+b'\n'),[])

    def test_speech_text_not_stripped(self):
        rows=self.parse(b'@voice {"stage":"realtime_user","text":"WebRtcNs_Create: literal user text"}\n')
        self.assertEqual(rows[0]['text'],'WebRtcNs_Create: literal user text')

if __name__=='__main__':unittest.main()
