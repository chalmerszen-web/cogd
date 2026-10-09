"""Host-only CTC prefix beam; no word lexicon, forced tokens or acoustic model."""
import math
import numpy as np

CLASSES = 14
NEG = -math.inf


def logadd(a, b):
    if a == NEG:
        return b
    if b == NEG:
        return a
    return max(a, b) + math.log1p(math.exp(-abs(a - b)))


class PrefixBeam:
    """Bounded input diagnostic, not a microphone/clock/confidence owner.

    Each prefix owns two path sums, ending in blank and nonblank. All classes
    compete, including OTHER and wrong tones. Pruning can lose probability;
    scores are not calibrated posteriors or a proof of global MAP decoding.
    """
    def __init__(self, width=8, max_frames=128):
        if not isinstance(width, int) or width <= 0:
            raise ValueError('Positive integer beam width required')
        if not isinstance(max_frames, int) or max_frames <= 0:
            raise ValueError('Positive integer frame bound required')
        self.width, self.max_frames = width, max_frames
        self.reset()

    def reset(self):
        self.frames = 0
        self.paths = {(): (0.0, NEG)}

    def ranked(self):
        return sorted(((prefix, logadd(*values)) for prefix, values in self.paths.items()),
                      key=lambda item: (-item[1], item[0]))

    def step(self, scores):
        scores = np.asarray(scores, dtype=np.float64)
        if (scores.shape != (CLASSES,) or np.isnan(scores).any() or
                np.isposinf(scores).any() or not np.isfinite(scores).any()):
            raise ValueError('Expected14 class scores; finite or negative infinity')
        if self.frames >= self.max_frames:
            raise ValueError('Input frame bound reached; reset before another input')
        maximum = float(scores.max())
        logs = scores - maximum - math.log(float(np.exp(scores - maximum).sum()))
        next_paths = {}

        def accumulate(prefix, value, blank):
            if value == NEG:
                return
            pair = next_paths.setdefault(prefix, [NEG, NEG])
            index = 0 if blank else 1
            pair[index] = logadd(pair[index], value)

        for prefix, (pb, pn) in self.paths.items():
            total = logadd(pb, pn)
            accumulate(prefix, total + float(logs[0]), True)
            for token in range(1, CLASSES):
                score = float(logs[token])
                if prefix and token == prefix[-1]:
                    accumulate(prefix, pn + score, False)
                    accumulate(prefix + (token,), pb + score, False)
                else:
                    accumulate(prefix + (token,), total + score, False)
        ranked = sorted(next_paths.items(),
                        key=lambda item: (-logadd(*item[1]), item[0]))
        self.paths = {prefix: tuple(pair) for prefix, pair in ranked[:self.width]}
        self.frames += 1
        return self.ranked()[0]

    def push(self, values):
        values = np.asarray(values)
        if values.ndim != 2 or values.shape[1] != CLASSES:
            raise ValueError('Expected frame x14 class scores')
        for row in values:
            self.step(row)
        return self.ranked()
