"""Offline checks for speculative protocol evidence; no key, network or audio IO."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/voice'))
from prefetch_probe import analyze


def row(kind, at, **fields):
    return dict(observed_s=at, event=dict(type=kind, **fields))


def speech(ident, text, at):
    return [row('input_audio_buffer.speech_started', at, item_id=ident),
            row('input_audio_buffer.committed', at+.2, item_id=ident),
            row('conversation.item.input_audio_transcription.completed', at+.25, item_id=ident, transcript=text)]


def response(ident, text, at, status='completed'):
    audio = row('response.audio.delta', at+.2, response_id=ident)
    audio['audio_pcm_bytes'] = 2400
    return [row('response.created', at, response=dict(id=ident)),
            row('response.audio_transcript.delta', at+.1, response_id=ident, delta=text),
            audio, row('response.done', at+.4, response=dict(id=ident, status=status))]


class PrefetchEvidence(unittest.TestCase):
    def test_ready_before_local_gate(self):
        events = speech('input', '介绍你自己。', 0)+response('first', '我是小言。', 1)
        result = analyze(dict(complete=True, transcripts=['介绍你自己。'], input_active_end_estimate_s=1.4),
                         events, '介绍你自己。', '小言', [])
        self.assertTrue(result['latest_candidate_valid'])
        self.assertTrue(result['latest_pcm_before_file_end'])
        self.assertFalse(result['playback_authorized'])

    def test_correction_invalidates_complete_and_late_old_audio(self):
        events = speech('blue', '设为蓝色。', 0)+response('old', '准备设为蓝色。', 1)
        events += speech('green', '改成绿色。', 2)
        late = row('response.audio.delta', 2.3, response_id='old');late['audio_pcm_bytes'] = 480
        events += [late]+response('new', '准备设为绿色。', 3)
        report = dict(complete=True, transcripts=['设为蓝色。', '改成绿色。'], input_active_end_estimate_s=3.4)
        result = analyze(report, events, '设为蓝色，改成绿色。', '绿', ['蓝'])
        self.assertTrue(result['latest_candidate_valid'])
        self.assertEqual(result['discarded_response_ids'], ['old'])
        self.assertEqual(result['responses']['old']['pcm_bytes'], 2880)
        missing = [e for e in events if not (e['event'].get('item_id') == 'green' and
                   e['event']['type'] == 'conversation.item.input_audio_transcription.completed')]
        self.assertFalse(analyze(report, missing, '设为蓝色，改成绿色。', '绿', ['蓝'])['latest_candidate_valid'])

    def test_cancelled_or_superseded_never_accepted(self):
        report = dict(complete=True, transcripts=['你好'], input_active_end_estimate_s=1.4)
        events = speech('input', '你好', 0)+response('a', '你好', 1, 'cancelled')
        self.assertFalse(analyze(report, events, '你好', '你好', [])['latest_candidate_valid'])
        events[-1]['event']['response']['status'] = 'completed'
        events.append(row('input_audio_buffer.speech_started', 2, item_id='later'))
        self.assertFalse(analyze(report, events, '你好', '你好', [])['latest_candidate_valid'])

    def test_unknown_or_reused_identity_rejected(self):
        report = dict(complete=True, transcripts=['你好'])
        with self.assertRaises(ValueError):
            analyze(report, [row('response.text.delta', 0, response_id='missing', delta='你好')], '你好', '你好', [])
        created = row('response.created', 0, response=dict(id='same'))
        with self.assertRaises(ValueError):
            analyze(report, [created, created], '你好', '你好', [])

    def test_old_generation_created_after_new_speech_keeps_old_revision(self):
        events = speech('old-input', '你好', 0)
        events.append(row('input_audio_buffer.speech_started', .5, item_id='new-input'))
        events += response('late-created', '你好', 1)
        result = analyze(dict(complete=True), events, '你好', '你好', [])
        self.assertEqual(result['responses']['late-created']['revision'], 1)
        self.assertIn('late-created', result['discarded_response_ids'])
        self.assertFalse(result['latest_candidate_valid'])


if __name__ == '__main__':
    unittest.main()
