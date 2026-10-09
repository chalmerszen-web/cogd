"""Locate the existing 180ms endpoint sweep in 16kHz acoustic evidence."""
import numpy as np
from scipy import signal


def find_endpoint_cue(recording, low, high):
    if len(recording) < 512 or high <= low:
        return dict(present=False, reason='No bounded cue search interval')
    frequency, time, power = signal.spectrogram(recording, fs=16000, nperseg=512,
                                               noverlap=432, mode='magnitude')
    band = (frequency >= 700) & (frequency <= 3000)
    peaks = frequency[band][np.argmax(power[band], axis=0)]
    candidates = []
    for start in np.arange(max(0, low), min(len(recording)/16000-.18, high), .005):
        age = time-start
        valid = (age >= .02) & (age <= .14)
        expected = 2600-1700*age/.18
        score = float(np.mean(abs(peaks[valid]-expected[valid]) < 150)) if valid.sum() >= 20 else 0
        candidates.append((score, float(start)))
    if not candidates:
        return dict(present=False, reason='No complete cue search window')
    score, start = max(candidates)
    alternative = max((s for s, x in candidates if abs(x-start) > .25), default=0)
    return dict(present=score >= .75 and alternative < .75, score=score,
                alternative_score=alternative, start_s=start, end_s=start+.18,
                method='Existing 2600-to-900Hz sweep; fixed .75 score and ambiguity rejection')
