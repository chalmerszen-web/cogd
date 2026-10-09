import copy
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/voice'))
import text_candidate_probe as probe


class TextCandidateTests(unittest.TestCase):
    def test_item_identity_and_content(self):
        case = probe.CASES[0]
        event = dict(type='conversation.item.created',
                     item=dict(probe.user_item(case)['item'], id='server_assigned', status='completed'))
        self.assertTrue(probe.item_matches(event, case))
        self.assertNotIn('id', probe.user_item(case)['item'])
        for field, value in [('id', ''), ('id', 'x'*65), ('status', 'in_progress'),
                             ('role', 'assistant'), ('content', []), ('type', 'function_call')]:
            altered = copy.deepcopy(event)
            altered['item'][field] = value
            self.assertFalse(probe.item_matches(altered, case))
        for invalid in ('', 'x'*513):
            with self.assertRaises(ValueError):
                probe.user_item(dict(case, text=invalid))

    def test_config_is_model_and_rate_specific(self):
        config = dict(probe.CONFIG, model=probe.MODEL)
        self.assertTrue(probe.config_matches(config))
        omitted = dict(config)
        omitted.pop('tools')
        omitted.pop('turn_detection')
        self.assertTrue(probe.config_matches(omitted))
        for field, value in [('model', 'other'), ('tools', [{}]), ('max_tokens', 4096),
                             ('turn_detection', {}), ('audio', {})]:
            self.assertFalse(probe.config_matches(dict(config, **{field: value})))

    def test_preflight_never_imports_network_or_key(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary) / 'plan'
            with patch.dict(sys.modules, {'websocket': None, 'qianwen_credentials': None}):
                result = probe.run(out, False)
            self.assertEqual(len(result['cases']), 3)
            self.assertFalse(result['sessions'])
            self.assertFalse(result['played'])
            with self.assertRaises(FileExistsError):
                probe.run(out, False)

    def test_action_ack_must_remain_parameter_free_and_future_tense(self):
        case = probe.CASES[1]
        self.assertTrue(probe.content_check(case, '嗯，我来调整一下灯光。'))
        for text in ('已调成蓝色。', '嗯，我来调整一下灯光。改成绿色。', '嗯，我来调整蓝色灯光。'):
            self.assertFalse(probe.content_check(case, text))


if __name__ == '__main__':
    unittest.main()
