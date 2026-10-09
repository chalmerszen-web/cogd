"""Offline timing/report contract checks; no serial or audio devices."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

from flow_latency import compare, load, trial_row


def trial(long=False):
    markers = {
        'asr_connect': 1100, 'asr_connected': 2000, 'asr_started': 2100, 'asr_audio': 2400,
        'vad_end': 4100, 'asr_done': 4250, 'asr_text': 4300, 'llm': 4400, 'llm_body_sent': 5000,
        'tts_connect': 5010, 'tts_connected': 5500, 'tts_started': 5550, 'llm_first_text': 5700,
        'tts_text': 5710, 'playback': 6000, 'llm_done': 9000 if long else 5800,
        'speaker_start': 6150, 'tts_done': 10000,
    }
    return {
        'id': 'first', 'wake_language': 'zh', 'complete': True, 'error': None,
        'voice': {'error': 'ok'}, 'status': {'version': 'test'},
        'wake': {'detected_at': 1000, 'record_at': 1300, 'end_at': 4000, 'cue_end_at': 4050},
        'wake_source': {'source_sha256': 'a' * 64, 'gain': 1},
        'utterance_source': {'source_sha256': 'b' * 64, 'gain': 1},
        'utterance_playback': {'finished': 10, 'device_name': 'test'},
        'events': [{'stage': stage, 'time_ms': time, 'observed': 12.5} for stage, time in markers.items()],
    }


def row(data=None, complete=True):
    return trial_row(data or trial(), 'test', complete)


class FlowLatencyTest(unittest.TestCase):
    def test_short_and_long_overlap_are_not_negative_phases(self):
        short, long = row(), row(trial(long=True))
        self.assertFalse(short['overlap']['pcm_before_llm_done'])
        self.assertEqual(short['overlap']['pcm_after_llm_done_s'], .2)
        self.assertTrue(long['overlap']['pcm_before_llm_done'])
        self.assertEqual(long['overlap']['pcm_lead_before_llm_done_s'], 3)
        for result in (short, long):
            self.assertEqual(result['metrics_s']['input_end_to_pcm_s'], 2.5)
            self.assertEqual(result['metrics_s']['input_end_to_speaker_s'], 2.65)
            self.assertEqual(result['metrics_s']['first_text_to_tts_submission_s'], .01)
            self.assertEqual(result['metrics_s']['capture_end_to_upload_end_s'], .1)
            self.assertEqual(result['metrics_s']['capture_end_to_cue_end_s'], .05)
            self.assertEqual(result['metrics_s']['cue_end_to_upload_end_s'], .05)
            self.assertEqual(result['metrics_s']['asr_after_upload_s'], .2)
            self.assertNotIn('capture_commit_s', result['metrics_s'])
            self.assertNotIn('asr_after_commit_s', result['metrics_s'])
            self.assertEqual(result['overlap']['tts_handshake_with_llm_s'], .49)
            self.assertNotIn('before_tts', result['metrics_s'])
            self.assertTrue(all(value is None or value >= 0 for value in result['metrics_s'].values()))

    def test_unequal_counts_use_matching_prefix_only(self):
        second = trial();second['id'] = 'second'
        result = compare([row(), row(second)], [row(trial(long=True))])
        self.assertEqual(result['compared_pairs'], 1)
        self.assertEqual(result['excluded_baseline'], 1)
        self.assertEqual(result['paired_metrics']['llm_s']['baseline']['n'], 1)
        with self.assertRaises(ValueError):
            compare([row(), row(second)], [row()], strict=True)

    def test_source_or_order_mismatch_does_not_skip_to_later_matches(self):
        different = trial();different['utterance_source']['source_sha256'] = 'c' * 64
        result = compare([row(), row()], [row(different), row()])
        self.assertEqual(result['matched_source_prefix'], 0)
        self.assertEqual(result['compared_pairs'], 0)
        self.assertFalse(result['paired_metrics'])

    def test_incomplete_and_failed_runs_retained_but_not_compared(self):
        bad = trial();bad['complete'] = False;bad['error'] = 'timeout'
        result = compare([row(), row()], [row(bad), row()])
        self.assertEqual(result['matched_source_prefix'], 2)
        self.assertEqual(result['compared_pairs'], 0)
        self.assertIsNotNone(result['candidate'][0]['metrics_s']['asr_handshake_s'])
        self.assertFalse(row(complete=False)['eligible'])
        with self.assertRaises(ValueError):
            compare([row()], [row(bad)], strict=True)

    def test_missing_markers_and_reversed_time_not_fabricated(self):
        old = trial();old['events'] = [e for e in old['events'] if e['stage'] not in ('llm_body_sent', 'tts_text')]
        result = row(old)
        self.assertIsNone(result['metrics_s']['final_body_to_first_text_s'])
        self.assertIsNone(result['metrics_s']['first_text_to_tts_submission_s'])
        self.assertTrue(result['eligible'])
        broken = trial();broken['events'][1]['time_ms'] = 1000
        result = row(broken)
        self.assertIsNone(result['metrics_s']['asr_handshake_s'])
        self.assertFalse(result['eligible'])
        reversed_text = trial()
        next(e for e in reversed_text['events'] if e['stage'] == 'tts_text')['time_ms'] = 5690
        result = row(reversed_text)
        self.assertEqual(result['signed_marker_offsets_s']['tts_submission_relative_first_text_s'], -.01)
        self.assertIsNone(result['metrics_s']['first_text_to_tts_submission_s'])
        self.assertFalse(result['eligible'])

    def test_malformed_saved_report_and_changed_conditions(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'report.json'
            path.write_text(json.dumps({'complete': True}), encoding='utf8')
            with self.assertRaises(ValueError):
                load([directory])
            path.write_text('{broken', encoding='utf8')
            with self.assertRaises(ValueError):
                load([directory])
        other = copy.deepcopy(trial());other['wake_source']['gain'] = 2
        self.assertEqual(compare([row()], [row(other)])['compared_pairs'], 0)
        other = trial();other['wake'] = ['invalid']
        self.assertFalse(row(other)['eligible'])

    def test_optional_upload_diagnostics_are_not_tail_or_server_timings(self):
        self.assertIsNone(row()['asr_upload_stats'])
        value = {'samples': 1600, 'chunks': 2, 'source_ms': 7,
                 'feed_ms': 25, 'wait_ms': 60, 'complete': True}
        data = trial()
        data['events'].append({'stage': 'asr_upload_stats', 'time_ms': 4099, 'text': json.dumps(value)})
        result = row(data)
        self.assertEqual(result['asr_upload_stats'], value)
        self.assertEqual(result['metrics_s'], row()['metrics_s'])
        self.assertFalse(result['diagnostic_issues'])
        value.update(samples=0, chunks=0, complete=False)
        data['events'][-1]['text'] = json.dumps(value)
        self.assertEqual(row(data)['asr_upload_stats'], value)

    def test_invalid_upload_diagnostics_do_not_fabricate_counts(self):
        good = {'samples': 1600, 'chunks': 2, 'source_ms': 7,
                'feed_ms': 25, 'wait_ms': 60, 'complete': True}
        variants = ['bad JSON', 'null', '{}']
        for key, value in [('samples', True), ('chunks', -1), ('source_ms', 1.5),
                           ('feed_ms', '25'), ('complete', 1), ('samples', 2049)]:
            variants.append(json.dumps({**good, key: value}))
        for text in variants:
            data = trial()
            data['events'].append({'stage': 'asr_upload_stats', 'time_ms': 4099, 'text': text})
            result = row(data)
            self.assertIsNone(result['asr_upload_stats'])
            self.assertTrue(result['diagnostic_issues'])
            self.assertTrue(result['eligible'])  # Optional diagnostics do not rewrite valid main timing.
        data = trial()
        data['events'].extend([{'stage': 'asr_upload_stats', 'time_ms': 4099, 'text': json.dumps(good)}] * 2)
        self.assertIsNone(row(data)['asr_upload_stats'])
        self.assertIn('Duplicate', row(data)['diagnostic_issues'][0])


if __name__ == '__main__':
    unittest.main()
