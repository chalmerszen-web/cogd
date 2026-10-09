"""Identity guards for observed ASR metadata; no credentials or network."""
import importlib.util
from pathlib import Path
import sys
import unittest

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root/'tools/voice'))
spec = importlib.util.spec_from_file_location('asr_manual_probe', root/'tools/voice/asr_manual_probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class CreatedItems(unittest.TestCase):
    def provisional(self):
        return dict(item=dict(object='realtime.item', type='message', status='in_progress',
                              role='assistant', content=[dict(type='input_audio')]))

    def test_observed_announcement_has_no_identity(self):
        message = self.provisional()
        self.assertEqual(probe.created_item_kind(message, None), 'provisional')
        self.assertNotIn('id', message['item'])
        with self.assertRaises(ValueError):
            probe.created_item_kind(message, 'confirmed')

    def test_announcements_cannot_impersonate_commits(self):
        message = self.provisional()
        message['item']['id'] = 'old'
        with self.assertRaises(ValueError):
            probe.created_item_kind(message, None)
        message['item']['role'] = 'user'
        self.assertEqual(probe.created_item_kind(message, 'old'), 'committed')
        with self.assertRaises(ValueError):
            probe.created_item_kind(message, 'new')

    def test_wrong_content_never_admitted(self):
        for kind in ('function_call', 'message'):
            message = self.provisional()
            message['item']['type'] = kind
            message['item']['content'] = [dict(type='text', text='claim completion')]
            with self.assertRaises(ValueError):
                probe.created_item_kind(message, None)


if __name__ == '__main__':
    unittest.main()
