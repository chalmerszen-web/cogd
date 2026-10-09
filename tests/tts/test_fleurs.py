"""Float WAV must retain speech amplitude when converted to training PCM16."""
import io
import json
import hashlib
from pathlib import Path
import sys
import unittest
import tempfile
import numpy as np
import soundfile as sf
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/tts'))
from fleurs_subset import decode_pcm,load_exclusions,possible_wake_text

class Conversion(unittest.TestCase):
    def test_float_wav_scales_and_saturates(self):
        stream=io.BytesIO()
        sf.write(stream,np.array([0.,.5,-.5,1.,-1.],dtype=np.float32),16000,format='WAV',subtype='FLOAT')
        pcm,rate=decode_pcm(stream.getvalue())
        np.testing.assert_array_equal(pcm,[0,16384,-16384,32767,-32768]);self.assertEqual(rate,16000)

    def test_integer_wav_retains_values(self):
        stream=io.BytesIO();expected=np.array([0,12000,-12000,32767,-32768],dtype=np.int16)
        sf.write(stream,expected,16000,format='WAV',subtype='PCM_16')
        pcm,_=decode_pcm(stream.getvalue());np.testing.assert_array_equal(pcm,expected)

    def test_silence_is_not_accepted_as_natural_speech(self):
        stream=io.BytesIO();sf.write(stream,np.zeros(8000),16000,format='WAV',subtype='FLOAT')
        with self.assertRaisesRegex(ValueError,'silent'):decode_pcm(stream.getvalue())

    def test_exclusions_include_all_old_splits_and_original_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'manifest.jsonl'
            rows=[dict(source_dataset='google/fleurs',split=split,
                clip_id=f'augmented-{split}',original_recording_id=f'original-{split}',
                pcm_sha256=f'pcm-{split}') for split in ('train','validation','test')]
            rows.append(dict(source_dataset='other',original_recording_id='other',pcm_sha256='other'))
            path.write_text('\n'.join(json.dumps(row) for row in rows)+'\n',encoding='utf8')
            ids,hashes,sources=load_exclusions([path])
            self.assertEqual(ids,{f'original-{s}' for s in ('train','validation','test')})
            self.assertEqual(hashes,{f'pcm-{s}' for s in ('train','validation','test')})
            self.assertEqual(sources[0]['sha256'],hashlib.sha256(path.read_bytes()).hexdigest())

    def test_possible_target_cannot_be_labelled_background_after_spacing(self):
        for text in ('你好，小言','你 好 小 言','你好小嚴','你好小妍'):
            self.assertTrue(possible_wake_text(text))
        self.assertFalse(possible_wake_text('这是一段普通讲话'))

    def test_exclusions_fail_closed_on_malformed_source(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'manifest.jsonl'
            path.write_text('{"source_dataset":"google/fleurs"}\n',encoding='utf8')
            with self.assertRaises(KeyError):load_exclusions([path])

if __name__=='__main__':unittest.main()
