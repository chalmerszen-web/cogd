"""Continuous finite-tail CTC oracle; all14 classes compete in beam8.

Four collapsed labels are enough for the wake-event suffix. Different earlier
histories merge into the same suffix probability. Each ending state also
retains its best-path label ages for the separate three-second span check;
these ages are NOT timestamps of all summed paths or calibrated posteriors.
"""
import math
import numpy as np
from ctc_beam import NEG, logadd


class SuffixBeam:
    def __init__(self, width=8):
        if not isinstance(width, int) or width <= 0:
            raise ValueError('Positive width required')
        self.width = width
        self.reset()

    def reset(self):
        # pb, pn, best blank/nonblank path, associated label ages.
        self.paths = {(): [0., NEG, 0., NEG, (), ()]}
        self.frames = 0
        self.log_offset = 0.
        self.viterbi_offset = 0.

    def ranked(self):
        return sorted(((prefix, logadd(row[0], row[1])) for prefix, row in self.paths.items()),
                      key=lambda pair: (-pair[1], pair[0]))

    def step(self, scores):
        scores = np.asarray(scores, dtype=np.float64)
        if (scores.shape != (14,) or np.isnan(scores).any() or np.isposinf(scores).any()
                or not np.isfinite(scores).any()):
            raise ValueError('Expected14 finite-or-negative-infinity scores')
        maximum = float(scores.max())
        logs = scores - maximum - math.log(float(np.exp(scores-maximum).sum()))
        next_paths = {}

        def add(prefix, probability, best_path, ages, blank):
            if probability == NEG:
                return
            row = next_paths.setdefault(prefix, [NEG, NEG, NEG, NEG, (), ()])
            slot = 0 if blank else 1
            row[slot] = logadd(row[slot], probability)
            if best_path > row[slot+2] or (best_path == row[slot+2] and ages < row[slot+4]):
                row[slot+2], row[slot+4] = best_path, ages

        for prefix, row in self.paths.items():
            pb, pn, vb, vn = row[:4]
            ab = tuple(min(255, age+1) for age in row[4])
            an = tuple(min(255, age+1) for age in row[5])
            total = logadd(pb, pn)
            if vb > vn or (vb == vn and ab < an):
                best, ages = vb, ab
            else:
                best, ages = vn, an
            add(prefix, total+float(logs[0]), best+float(logs[0]), ages, True)
            for token in range(1, 14):
                score = float(logs[token])
                tail = (prefix+(token,))[-4:]
                if prefix and prefix[-1] == token:
                    add(prefix, pn+score, vn+score, an, False)
                    add(tail, pb+score, vb+score, (ab+(0,))[-4:], False)
                else:
                    add(tail, total+score, best+score, (ages+(0,))[-4:], False)
        ranked = sorted(next_paths.items(), key=lambda pair: (-logadd(pair[1][0], pair[1][1]), pair[0]))
        offset = logadd(ranked[0][1][0], ranked[0][1][1])
        path_offset = max(max(row[2:4]) for _, row in ranked[:self.width])
        self.paths = {}
        for prefix, row in ranked[:self.width]:
            row[:2] = [score-offset for score in row[:2]]
            row[2:4] = [score-path_offset for score in row[2:4]]
            self.paths[prefix] = row
        self.frames += 1
        self.log_offset += offset
        self.viterbi_offset += path_offset
        prefix = ranked[0][0]
        row = self.paths[prefix]
        slot = 0 if row[2] > row[3] or (row[2] == row[3] and row[4] < row[5]) else 1
        return prefix, row[slot+4]
