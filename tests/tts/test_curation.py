import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'training/tts'))
from curate import target_positions,target_prefix,compact

class TargetLocation(unittest.TestCase):
    def test_exact_word_can_follow_filler_in_source(self):
        self.assertEqual(target_positions('哈你好小贤今日天气好好','yue'),[1])
        self.assertEqual(target_positions('你好小言','zh'),[0])

    def test_wrong_tone_and_wrong_name_are_still_rejected(self):
        for text in ('你好小燕','你好小二','你好小嚴','你好小顏'):
            self.assertEqual(target_positions(text,'yue'),[])

    def test_duplicate_targets_remain_ambiguous(self):
        self.assertEqual(target_positions('你好小言你好小賢','yue'),[0,4])

    def test_crop_still_requires_only_the_four_syllables(self):
        self.assertFalse(target_prefix('哈你好小贤','yue') and len(compact('哈你好小贤'))==4)

if __name__=='__main__':unittest.main()
