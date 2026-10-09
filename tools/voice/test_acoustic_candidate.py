"""Regression for truncated TLS playback and punctuation-only ASR evidence."""
import unittest
from analyze_continuous import answer_candidate


class AcousticCandidateTest(unittest.TestCase):
    def result(self, text='我是小言。', terminal='@done', matched=True, onsets=None):
        return answer_candidate({'terminal':terminal}, matched,
                                [4.0] if onsets is None else onsets, text, 3.2)

    def test_completed_words_retain_measured_candidate(self):
        r=self.result()
        self.assertEqual(r['status'],'candidate')
        self.assertAlmostEqual(r['acoustic_answer_latency_candidate_s'],.8)

    def test_punctuation_and_whitespace_are_not_speech(self):
        for text in ('','.','。','…','！？', '\n  ', ' . 。 '):
            with self.subTest(text=text):
                r=self.result(text=text)
                self.assertEqual(r['status'],'unknown')
                self.assertNotIn('acoustic_answer_latency_candidate_s',r)

    def test_network_error_with_actual_words_is_still_partial(self):
        for terminal in ('@error network','@error cancelled',None,''):
            r=self.result(terminal=terminal)
            self.assertEqual(r['status'],'unknown')
            self.assertNotIn('acoustic_answer_latency_candidate_s',r)

    def test_acknowledgement_does_not_become_answer(self):
        for text in ('收到。','收到，我来处理。','收到，等我处理。'):
            self.assertEqual(self.result(text=text)['status'],'unknown')

    def test_speech_cannot_override_failed_waveform_or_missing_onset(self):
        self.assertEqual(self.result(matched=False)['status'],'unknown')
        self.assertEqual(self.result(onsets=[])['status'],'unknown')


if __name__=='__main__':unittest.main()
