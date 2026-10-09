"""Memory completion must follow durable state, not a model's spoken claim."""
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/voice'))
from memory_evidence import memory_contains, memory_effect

FACT = '小星星'
SAVED = dict(present=True, through_event_id='device-42', text='用户喜欢小星星。')


def tool(value):
    return '@tool agent_context_summary_set ' + json.dumps(value, ensure_ascii=False)


class MemoryEvidence(unittest.TestCase):
    def test_requires_durable_metadata_and_exact_fact(self):
        self.assertTrue(memory_contains(SAVED, FACT))
        for value in (None, [], {}, dict(SAVED, present=1), dict(SAVED, text=None),
                      dict(SAVED, through_event_id=''), dict(SAVED, through_event_id=42),
                      dict(SAVED, text='没有相关记录')):
            self.assertFalse(memory_contains(value, FACT), value)
        self.assertFalse(memory_contains(SAVED, ''))

    def test_spoken_success_is_not_a_write(self):
        lines = ['已记住你喜欢小星星。', '@done', '@tool agent_context_summary_get ' + json.dumps(SAVED)]
        self.assertFalse(memory_effect(lines, FACT))
        self.assertTrue(memory_effect(lines, FACT, already_saved=True))

    def test_rejected_tool_preserves_only_verified_prior_state(self):
        rejected = tool(dict(executed=False, error='invalid', detail=FACT))
        self.assertFalse(memory_effect([rejected], FACT))
        self.assertTrue(memory_effect([rejected], FACT, already_saved=True))
        self.assertTrue(memory_effect([tool(SAVED), rejected], FACT))

    def test_latest_successful_overwrite_controls_result(self):
        overwrite = tool(dict(SAVED, text='用户喜欢另一首歌。'))
        self.assertFalse(memory_effect([tool(SAVED), overwrite], FACT))
        self.assertTrue(memory_effect([overwrite, tool(SAVED)], FACT))

    def test_corruption_fails_closed_until_verified_result(self):
        for bad in ('{', 'null', '[]', 'true', '"saved"'):
            line = '@tool agent_context_summary_set ' + bad
            self.assertFalse(memory_effect([tool(SAVED), line], FACT))
            self.assertTrue(memory_effect([line, tool(SAVED)], FACT))


if __name__ == '__main__':
    unittest.main()
