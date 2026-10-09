import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/voice'))
from fixture_acceptance import check_content, check_runtime_health


class ContentChecks(unittest.TestCase):
    def test_runtime_errors_are_separate_from_correct_effects(self):
        lines=['E (140027) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:',
               '@tool device_light_set_rgb {"ok":true,"r":0,"g":255,"b":0}',
               '灯光现在是绿色。', '@done']
        self.assertTrue(check_content(dict(raw_lines=lines), 'correction')['passed'])
        self.assertEqual(check_runtime_health(lines),
                         dict(passed=False, counts=dict(watchdog=1, panic=0, reboot=0)))
        for signature, name in [('Guru Meditation Error: Core 0 panic', 'panic'),
                                ('abort() was called at PC 0x42001000', 'panic'),
                                ('rst:0x3 (RTC_SW_SYS_RST),boot:0xc', 'reboot'),
                                ('ESP-ROM:esp32c3-api1-20210207', 'reboot')]:
            health=check_runtime_health([signature])
            self.assertFalse(health['passed']);self.assertEqual(health['counts'][name],1)
        self.assertTrue(check_runtime_health([
            '@voice {"stage":"fast_reply","text":"Task watchdog got triggered."}',
            '什么是 watchdog？', '@done'])['passed'])

    def test_classic_tool_timing_is_not_a_local_result(self):
        trial = dict(events=[dict(stage='tool_done'), dict(stage='tool_done')],
                     raw_lines=['@tool device_light_set_rgb {"ok":true,"r":0,"g":255,"b":0}',
                                '已经改成绿色。'])
        self.assertTrue(check_content(trial, 'correction')['passed'])
        # Do not relax colour confirmation or accept an unidentified local
        # payload merely because the classic tool line is also present.
        generic = dict(trial, raw_lines=[trial['raw_lines'][0], '灯光设置好了。'])
        self.assertEqual(check_content(generic, 'correction')['reasons'],
                         ['missing_green_confirmation'])
        for payload in (None, 'invalid', '{"ok":true,"r":0,"g":255,"b":0}'):
            mixed = dict(trial, events=trial['events']+[dict(stage='tool_done', text=payload)])
            self.assertFalse(check_content(mixed, 'correction')['passed'])

    def test_local_correction_requires_final_effect_not_a_candidate(self):
        events=[dict(stage='fast_local_intent',text='device_light_set_rgb'),
                dict(stage='tool_done',text='{"ok":true,"r":0,"g":255,"b":0}'),
                dict(stage='fast_reply',text='灯光设置好了。')]
        trial=dict(events=events,light_readback=dict(r=0,g=255,b=0))
        self.assertTrue(check_content(trial,'correction')['passed'])
        for index in range(3):
            bad=dict(trial,events=events[:index]+events[index+1:])
            self.assertFalse(check_content(bad,'correction')['passed'])
        self.assertFalse(check_content(dict(trial,light_readback=None),'correction')['passed'])
        self.assertFalse(check_content(dict(trial,light_readback=dict(r=0,g=0,b=255)),'correction')['passed'])
        for result in ('{"ok":true,"r":0,"g":0,"b":255}', 'invalid',
                       '{"executed":false,"error":"cancelled"}'):
            bad=dict(trial,events=[events[0],dict(stage='tool_done',text=result),events[2]])
            self.assertFalse(check_content(bad,'correction')['passed'])
        for colour in ((0,0,255),(0,255,0)):
            line='@tool device_light_set_rgb {"ok":true,"r":%d,"g":%d,"b":%d}' % colour
            self.assertFalse(check_content(dict(trial,raw_lines=[line]),'correction')['passed'])

    def test_local_light_requires_effect_readback_and_speech(self):
        events=[dict(stage='fast_local_intent',text='device_light_set_rgb'),
                dict(stage='tool_done',text='{"ok":true,"r":0,"g":0,"b":255}'),
                dict(stage='fast_reply',text='灯光设置好了。')]
        trial=dict(events=events,light_readback=dict(r=0,g=0,b=255))
        self.assertTrue(check_content(trial,'blue')['passed'])
        for index in range(3):
            bad=dict(trial,events=events[:index]+events[index+1:])
            self.assertFalse(check_content(bad,'blue')['passed'])
        self.assertFalse(check_content(dict(trial,light_readback=dict(r=0,g=255,b=0)),'blue')['passed'])

    def test_greeting(self):
        for text, good in [('我是小言，你的设备助手。', True), ('我来。', False),
                           ('我是小言，临时转写助手。', False)]:
            trial = {'events': [dict(stage='fast_reply', text=text)]}
            self.assertEqual(check_content(trial, 'greeting')['passed'], good)

    def test_correction_requires_action_and_speech(self):
        trial = {'raw_lines': ['@tool device_light_set_rgb {"ok":true,"r":0,"g":255,"b":0}',
                               '已设为绿色，蓝色没有设。']}
        self.assertTrue(check_content(trial, 'correction')['passed'])
        trial['events'] = [dict(stage='progress_start', text='我来把灯改成黑色。')]
        self.assertFalse(check_content(trial, 'correction')['passed'])
        trial['events'][0]['text'] = '我来调整灯光。'
        self.assertTrue(check_content(trial, 'correction')['passed'])
        trial['events'][0]['text'] = '我来把灯调成绿色。'
        self.assertTrue(check_content(trial, 'correction')['passed'])
        trial['events'].append(dict(stage='progress_transport', text='omni_manual_draft'))
        self.assertFalse(check_content(trial, 'correction')['passed'])
        trial['events'][0]['text'] = '我来调整灯光。'
        trial['raw_lines'].insert(0, '@tool device_light_set_rgb {"ok":true,"r":0,"g":0,"b":255}')
        self.assertFalse(check_content(trial, 'correction')['passed'])

    def test_greeting_keeps_requested_language(self):
        replies = ['我係ESP-HI助手小言，專幫你搞掂日常嘢。',
                   '我係ESP-HI助手小言，隨時幫你。',
                   '我係ESP-HI助手小言，專幫手搞掂所有事。']
        for text in replies:
            trial = dict(prompt_text='请用一句话介绍你自己。',
                         events=[dict(stage='fast_reply', text=text)])
            self.assertIn('unrequested_cantonese_in_mandarin_fixture',
                          check_content(trial, 'greeting')['reasons'])
            trial['prompt_text'] = '请用一句广东话介绍你自己。'
            self.assertTrue(check_content(trial, 'greeting')['passed'])
        trial = dict(prompt_text='请用一句话介绍你自己。',
                     events=[dict(stage='fast_reply', text='我是ESP-HI助手小言。')])
        self.assertTrue(check_content(trial, 'greeting')['passed'])

    def test_empty_and_malformed(self):
        self.assertFalse(check_content({}, 'correction')['passed'])
        trial = {'raw_lines': ['@tool device_light_set_rgb invalid', '绿色']}
        self.assertFalse(check_content(trial, 'correction')['passed'])

    def test_rejected_call_is_not_an_effect_or_success(self):
        trial = {'raw_lines': [
            '@tool device_light_set_rgb {"executed":false,"error":"argument"}', '绿色']}
        self.assertFalse(check_content(trial, 'correction')['passed'])
        trial['raw_lines'].append('@tool device_light_set_rgb {"ok":true,"r":0,"g":255,"b":0}')
        result = check_content(trial, 'correction')
        self.assertTrue(result['passed'])
        self.assertEqual(result['rejected_tool_calls'], 1)
        trial['raw_lines'].append('@tool device_light_set_rgb {"ok":true,"r":0,"g":0,"b":255}')
        self.assertFalse(check_content(trial, 'correction')['passed'])


if __name__ == '__main__':
    unittest.main()
