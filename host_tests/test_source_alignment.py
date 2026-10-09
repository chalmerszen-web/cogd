"""Acoustic timing must ignore file padding, not quiet speech or clock errors."""
from pathlib import Path
import sys,unittest

import numpy as np
from scipy import signal

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/voice'))
from analyze_continuous import align_repeated,RATE


class SourceAlignment(unittest.TestCase):
    @staticmethod
    def source(padding=True):
        rng=np.random.default_rng(714)
        speech=signal.sosfiltfilt(signal.butter(4,[350,3300],fs=RATE,btype='bandpass',output='sos'),
                                rng.standard_normal(round(1.4*RATE)))
        t=np.arange(len(speech))/RATE
        speech*=.01*(.2+.8*np.sin(2*np.pi*2.8*t)**2)
        return np.r_[np.zeros(round(.12*RATE)),speech,np.zeros(round(.8*RATE))] if padding else speech

    @staticmethod
    def recording(source,*,cues=False):
        starts=[.73,4.91,9.26]
        recording=np.random.default_rng(25).normal(0,.000001,round(13*RATE))
        for at in starts:
            offset=round(at*RATE)
            recording[offset:offset+len(source)]+=source
            if cues:
                # A real, loud response/cue inside the WAV's silent suffix.
                t=np.arange(round(.18*RATE))/RATE
                cue=.65*signal.chirp(t,2600,t[-1],900)*np.sin(np.pi*np.arange(len(t))/len(t))**2
                begin=offset+round(1.85*RATE)
                recording[begin:begin+len(cue)]+=cue
        return starts,recording

    def check_offsets(self,result,expected):
        self.assertTrue(result['accepted'])
        self.assertTrue(all(c['accepted'] for c in result['copies']))
        for copy,start in zip(result['copies'],expected):
            self.assertAlmostEqual(copy['source_start_recording_s'],start,delta=.002)

    def test_loud_reply_in_digital_padding_does_not_hide_input(self):
        source=self.source()
        starts,recording=self.recording(source,cues=True)
        self.check_offsets(align_repeated(source,recording,[100+x for x in starts]),starts)

    def test_unpadded_source_keeps_original_clock(self):
        source=self.source(padding=False)
        starts,recording=self.recording(source)
        self.check_offsets(align_repeated(source,recording,[100+x for x in starts]),starts)

    def test_quiet_nonzero_edges_are_preserved(self):
        source=self.source()
        source[17]=1/32768
        source[-31]=-1/32768
        starts,recording=self.recording(source)
        result=align_repeated(source,recording,[100+x for x in starts])
        self.check_offsets(result,starts)
        span=result['reference_samples']
        self.assertLessEqual(span[0],17)
        self.assertGreater(span[1],len(source)-31)

    def test_silence_cannot_match(self):
        result=align_repeated(np.zeros(RATE),np.zeros(10*RATE),[100,103,106])
        self.assertFalse(result['accepted'])

    def test_unrelated_recording_rejected(self):
        source=self.source()
        recording=np.random.default_rng(93).normal(0,.01,13*RATE)
        self.assertFalse(align_repeated(source,recording,[100,104,108])['accepted'])


if __name__=='__main__':unittest.main()
