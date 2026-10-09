"""Offline checks for the file-only protocol probe; no credentials/network."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import wave

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('manual_draft_probe', root/'tools/voice/manual_draft_probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class ManualDraftProbe(unittest.TestCase):
    def test_preview_replaces_revisable_suffix(self):
        self.assertEqual(probe.preview(dict(text='请把灯', stash='调成蓝色')), '请把灯调成蓝色')
        self.assertEqual(probe.preview(dict(text='请把灯', stash='调成绿色')), '请把灯调成绿色')
        with self.assertRaises(ValueError):
            probe.preview(dict(text='请把灯'))
        self.assertIsNone(probe.preview(dict(text='灯'*65, stash='')))

    def test_comparison_preserves_meaning(self):
        self.assertEqual(probe.normalized('  你好。\n'), probe.normalized('你好'))
        for left, right in [('不要蓝色', '要蓝色'), ('1.2', '12'), ('a b', 'ab'),
                            ('蓝色，不对，绿色', '蓝色'), ('"绿色"', '绿色')]:
            self.assertNotEqual(probe.normalized(left), probe.normalized(right))

    def test_preflight_never_submits_or_reuses_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            source, out = Path(temp)/'source.wav', Path(temp)/'out'
            with wave.open(str(source), 'wb') as wav:
                wav.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
                wav.writeframes(b'\x00\x00'*512)
            report = probe.run(source, out)
            self.assertEqual(report['attempts'], 0)
            self.assertEqual(report['source_samples'], 512)
            self.assertFalse(report['committed'] or report['played'] or report['tools_executed'])
            with self.assertRaises(FileExistsError):
                probe.run(source, out)
            overlap=probe.run(source,Path(temp)/'overlap',strategy='audio',commit_overlap=True)
            self.assertEqual(overlap['tail_samples'],3200)
            self.assertEqual(overlap['attempts'],0)
            with self.assertRaises(ValueError):
                probe.run(source,Path(temp)/'bad',strategy='text',commit_overlap=True)


if __name__ == '__main__':
    unittest.main()
