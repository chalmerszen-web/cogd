"""Offline regression for the actual extended ASR VAD configuration receipt."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
import wave

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/voice"))
from asr_vad_probe import PREPARED_IDS, observe, sha, valid_session

OBSERVED = json.loads('''{"model":"qwen3-asr-flash-realtime","modalities":["text"],
 "input_audio_format":"pcm","sample_rate":16000,
 "turn_detection":{"type":"server_vad","threshold":0.2,"silence_duration_ms":700,
 "create_response":true,"interrupt_response":true}}''')


class SessionEcho(unittest.TestCase):
    def test_observed_provider_extensions(self):
        self.assertTrue(valid_session(OBSERVED))

    def test_other_model_audio_and_rate_are_rejected(self):
        for name, value in (("model", "qwen3.5-omni-flash-realtime"),
                            ("modalities", ["text", "audio"]), ("sample_rate", 8000),
                            ("input_audio_format", "pcm16")):
            with self.subTest(name=name):
                item = copy.deepcopy(OBSERVED)
                item[name] = value
                self.assertFalse(valid_session(item))

    def test_selected_vad_settings_cannot_be_overridden(self):
        for name, value in (("type", "client_vad"), ("threshold", 0.0),
                            ("silence_duration_ms", 800)):
            with self.subTest(name=name):
                item = copy.deepcopy(OBSERVED)
                item["turn_detection"][name] = value
                self.assertFalse(valid_session(item))
        item = copy.deepcopy(OBSERVED)
        item["turn_detection"] = None
        self.assertFalse(valid_session(item))


class PreparedInputs(unittest.TestCase):
    def make(self, directory):
        directory.mkdir()
        pcm = bytes(8064*2)
        fixtures = []
        for name in PREPARED_IDS:
            with wave.open(str(directory/(name+'.wav')), 'wb') as w:
                w.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
                w.writeframes(pcm)
            fixtures.append(dict(id=name, input_file=name+'.wav', samples=8064, pcm_sha256=sha(pcm),
                original_samples=64, extra_tail_ms=500, source={'test': True}, expected='测试'))
        report = dict(prepared_asr_inputs=1, cloud_prerequisites_pass=True, fixtures=fixtures)
        self.save(directory, report)
        return report

    def save(self, directory, report):
        (directory/'report.json').write_text(json.dumps(report), encoding='utf8')

    def test_prepared_samples_are_not_padded_again(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.make(root/'input')
            report = observe(root/'out', root/'input', False, prepared=True)
            self.assertEqual(report['attempts'], 0)
            self.assertEqual([f['samples'] for f in report['fixtures']], [8064]*6)

    def test_explicit_fixture_order_is_bounded_and_still_verified(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data=self.make(root/'input')
            data={'fixtures':data['fixtures'][:3]}
            ids=['short_question','long_question','captured_question']
            for row,ident in zip(data['fixtures'],ids):row['id']=ident
            self.save(root/'input',data)
            report=observe(root/'out',root/'input',False,fixture_ids=ids)
            self.assertEqual(report['attempts'],0)
            self.assertEqual([f['id'] for f in report['fixtures']],ids)
            self.assertEqual([f['samples'] for f in report['fixtures']],[8064+11200]*3)
            for number,bad in enumerate(([],['x']*7,[''],ids[::-1])):
                with self.assertRaises(ValueError):
                    observe(root/f'bad-{number}',root/'input',False,fixture_ids=bad)

    def test_failed_gate_and_malformed_inputs_stop_before_network(self):
        for fault in ('gate', 'order', 'tail', 'hash', 'path'):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                report = self.make(root/'input')
                if fault == 'gate': report['cloud_prerequisites_pass'] = False
                if fault == 'order': report['fixtures'].reverse()
                if fault == 'tail': report['fixtures'][0]['original_samples'] += 1
                if fault == 'hash': report['fixtures'][0]['pcm_sha256'] = 'wrong'
                if fault == 'path': report['fixtures'][0]['input_file'] = '../outside.wav'
                self.save(root/'input', report)
                with self.assertRaises(ValueError):
                    observe(root/'out', root/'input', False, prepared=True)


if __name__ == "__main__":
    unittest.main()
