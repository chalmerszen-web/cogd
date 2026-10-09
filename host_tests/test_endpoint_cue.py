import sys
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import numpy as np
from scipy import signal
import soundfile as sf
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/voice'))
from endpoint_cue import find_endpoint_cue
from analyze_continuous import analyze, sha, voice_onsets


def cue(recording, start):
    t = np.arange(2880)/16000
    recording[round(start*16000):round(start*16000)+len(t)] += .15*signal.chirp(t, 2600, .18, 900)


class CueExclusion(unittest.TestCase):
    def test_local_light_fallback_does_not_count_endpoint_sweep(self):
        class Recognizer:
            def create_stream(self):
                return SimpleNamespace(accept_waveform=lambda rate, pcm: None,
                    result=SimpleNamespace(text="灯光设置好了。"))
            def decode_stream(self, stream):
                pass
        cases = [('present', True, '@done'), ('missing', True, '@done'),
                 ('ambiguous', True, '@done'), ('present', False, '@done'),
                 ('present', True, '@error protocol')]
        for kind, matched, terminal in cases:
            with self.subTest(kind=kind, matched=matched, terminal=terminal), tempfile.TemporaryDirectory() as tmp:
                p = Path(tmp)
                source = .1*np.sin(2*np.pi*600*np.arange(8000)/16000)
                recording = np.zeros(48000);recording[:8000] = source
                if kind == 'present':
                    cue(recording, .9)
                elif kind == 'ambiguous':
                    cue(recording, .7);cue(recording, 1.2)
                t = np.arange(12800)/16000
                recording[25600:38400] = .1*np.sin(2*np.pi*500*t)+.06*np.sin(2*np.pi*1200*t)
                sf.write(p/'prompt.wav', source, 16000, subtype='PCM_16')
                sf.write(p/'speaker.wav', recording, 16000, subtype='PCM_16')
                (p/'model.int8.onnx').write_bytes(b'mock');(p/'tokens.txt').write_text('mock')
                events = [dict(stage=stage,time_ms=ms,observed=100+ms/1000,text=text)
                    for stage,ms,text in [('vad_end',800,''),('asr_text',1400,''),
                        ('candidate_miss',1450,'final_local_light'),('playback',1550,'')]]
                attempt = dict(events=events,attempt=1,terminal=terminal,
                    prompt_playback=dict(source_started=100,finished=100.5),
                    prompt_sha256=sha(p/'prompt.wav'))
                report = dict(ended=True,external_recording=dict(sha256=sha(p/'speaker.wav')),
                    trials=[dict(round=1,language='zh',attempts=[attempt])])
                (p/'report.json').write_text(json.dumps(report),encoding='utf8')
                fit = dict(accepted=True,copies=[dict(source_start_recording_s=0,accepted=matched)])
                with patch('analyze_continuous.align_repeated',return_value=fit):
                    analyze(p,Recognizer(),p,'analysis.json')
                row = json.loads((p/'analysis.json').read_text())['trials'][0]
                self.assertEqual(row['endpoint_cue_acoustic']['present'],kind == 'present')
                if kind == 'present':
                    self.assertGreater(row['offline_asr']['crop_start_recording_s'],1.15)
                if kind == 'present' and matched and terminal == '@done':
                    self.assertEqual(row['status'],'candidate')
                    self.assertGreaterEqual(row['acoustic_answer_latency_candidate_s'],1.05)
                    self.assertLessEqual(row['acoustic_answer_latency_candidate_s'],1.11)
                else:
                    self.assertEqual(row['status'],'unknown')
                    self.assertNotIn('acoustic_answer_latency_candidate_s',row)

    def test_sweep_is_not_a_spoken_answer(self):
        y = np.zeros(48000)
        cue(y, .9)
        t = np.arange(12800)/16000
        y[25600:38400] = .1*np.sin(2*np.pi*500*t)+.06*np.sin(2*np.pi*1200*t)
        before = voice_onsets(y, .7, 2.5)['candidate_onsets_recording_s'][0]
        self.assertLess(before, 1.1)  # reproduces the false sub-second result
        match = find_endpoint_cue(y, .5, 1.5)
        self.assertTrue(match['present'])
        self.assertAlmostEqual(match['start_s'], .9, delta=.02)
        after = voice_onsets(y, match['end_s']+.1, 2.5)['candidate_onsets_recording_s'][0]
        self.assertGreaterEqual(after, 1.55)
        self.assertLessEqual(after, 1.61)

    def test_repeated_cues_are_ambiguous(self):
        y = np.zeros(48000);cue(y, .7);cue(y, 1.2)
        match = find_endpoint_cue(y, .3, 1.5)
        self.assertFalse(match['present'])
        self.assertGreaterEqual(match['alternative_score'], .75)

    def test_silence_and_steady_tone_are_not_a_sweep(self):
        self.assertFalse(find_endpoint_cue(np.zeros(32000), .2, 1.5)['present'])
        y = .2*np.sin(2*np.pi*1000*np.arange(32000)/16000)
        self.assertFalse(find_endpoint_cue(y, .2, 1.5)['present'])

    def test_missing_or_empty_interval_stays_unknown(self):
        for y, lo, hi in [(np.zeros(20), 0, 1), (np.zeros(32000), 2, 1),
                          (np.zeros(32000), 2, 3)]:
            self.assertFalse(find_endpoint_cue(y, lo, hi)['present'])

    def test_candidate_receipt_excludes_cue_before_later_final_tts(self):
        # Exercise the full analyzer's progress path. Only source alignment
        # and ASR are stubbed; cue detection, crop bounds and energy are real.
        class Recognizer:
            def create_stream(self):
                return SimpleNamespace(accept_waveform=lambda rate, pcm: None,
                    result=SimpleNamespace(text="嗯，我来调整一下灯光。"))
            def decode_stream(self, stream):
                pass
        for matched, has_cue in [(True, True), (False, True), (True, False)]:
            with self.subTest(matched=matched, has_cue=has_cue), tempfile.TemporaryDirectory() as tmp:
                p = Path(tmp)
                source = .1*np.sin(2*np.pi*600*np.arange(8000)/16000)
                recording = np.zeros(80000);recording[:8000] = source
                if has_cue:
                    cue(recording, .9)
                for start, length in [(1.5, .6), (3.2, .8)]:
                    t = np.arange(round(length*16000))/16000
                    recording[round(start*16000):round(start*16000)+len(t)] = .1*np.sin(2*np.pi*500*t)
                sf.write(p/'prompt.wav', source, 16000, subtype='PCM_16')
                sf.write(p/'speaker.wav', recording, 16000, subtype='PCM_16')
                (p/'model.int8.onnx').write_bytes(b'mock');(p/'tokens.txt').write_text('mock')
                events = [dict(stage=stage, time_ms=ms, observed=100+ms/1000, text=text)
                    for stage, ms, text in [('vad_end', 800, ''), ('asr_text', 900, ''),
                        ('candidate_play', 1450, '嗯，我来调整一下灯光。'),
                        ('progress_start', 1450, '嗯，我来调整一下灯光。'),
                        ('progress_generated', 1450, '嗯，我来调整一下灯光。'),
                        ('progress_end', 2300, ''), ('playback', 3150, '')]]
                attempt = dict(events=events, attempt=1, terminal='@done',
                    prompt_playback=dict(source_started=100, finished=100.5),
                    prompt_sha256=sha(p/'prompt.wav'))
                report = dict(ended=True, external_recording=dict(sha256=sha(p/'speaker.wav')),
                    trials=[dict(round=1, language='zh', attempts=[attempt])])
                (p/'report.json').write_text(json.dumps(report), encoding='utf8')
                fit = dict(accepted=True, copies=[dict(source_start_recording_s=0, accepted=matched)])
                with patch('analyze_continuous.align_repeated', return_value=fit):
                    analyze(p, Recognizer(), p, 'analysis.json')
                row = json.loads((p/'analysis.json').read_text(encoding='utf8'))['trials'][0]
                self.assertEqual(row['endpoint_cue_acoustic']['present'], has_cue)
                progress = row['progress_acoustic']
                if matched and has_cue:
                    self.assertGreater(progress['energy_ack_latency_candidate_s'], .9)
                    self.assertGreater(progress['offline_asr']['crop_s'][0], 1.15)
                else:
                    self.assertNotIn('energy_ack_latency_candidate_s', progress)
                    self.assertNotIn('ack_latency_candidate_s', progress)


if __name__ == '__main__':
    unittest.main()
