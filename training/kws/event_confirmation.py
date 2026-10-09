"""Causal matching of accepted detector events, with no score/threshold search."""
import numpy as np

MAX_GAP = 4096


def match_events(first, second):
    """Return later endpoints; each input event is owned by at most one match."""
    pending = [None, None]
    result = []
    merged = sorted([(int(end), 0) for end in first] +
                    [(int(end), 1) for end in second])
    for end, branch in merged:
        for index, value in enumerate(pending):
            if value is not None and end - value > MAX_GAP:
                pending[index] = None
        pending[branch] = end
        if all(value is not None for value in pending):
            result.append(end)
            pending = [None, None]
    return result


def classify_event_lists(old, new, label, language, low, high):
    """Same accepted-window statistics as the existing score classifier."""
    positive = {lang: dict(total=0, baseline_valid=0, candidate_valid=0, retained=0,
        lost=0, gained=0, baseline_premature=0, candidate_premature=0, new_premature=0)
        for lang in ('zh', 'yue')}
    negative = dict(total=0, baseline_triggered=0, candidate_triggered=0, new_triggered=0)
    rows = []
    if not len(old) == len(new) == len(label) == len(language) == len(low) == len(high):
        raise ValueError('Event and metadata lengths differ')
    for i, (b, c) in enumerate(zip(old, new)):
        if label[i]:
            bv = any(int(low[i]) <= end <= int(high[i]) + 12800 for end in b)
            cv = any(int(low[i]) <= end <= int(high[i]) + 12800 for end in c)
            bp = sum(end < int(low[i]) for end in b)
            cp = sum(end < int(low[i]) for end in c)
            p = positive[str(language[i])]
            p['total'] += 1
            p['baseline_valid'] += int(bv)
            p['candidate_valid'] += int(cv)
            p['retained'] += int(bv and cv)
            p['lost'] += int(bv and not cv)
            p['gained'] += int(cv and not bv)
            p['baseline_premature'] += bp
            p['candidate_premature'] += cp
            p['new_premature'] += int(cp > bp)
        else:
            negative['total'] += 1
            negative['baseline_triggered'] += int(bool(b))
            negative['candidate_triggered'] += int(bool(c))
            negative['new_triggered'] += int(bool(c) and not b)
        if b != c:
            rows.append(dict(index=i, baseline=b, candidate=c, label=int(label[i]),
                             language=str(language[i])))
    return dict(positive=positive, negative=negative, changed_rows=rows)


def delay_report(old, new, label, language, low, high):
    result = {}
    for lang in ('zh', 'yue'):
        delays = []
        for i in np.flatnonzero(np.asarray(label, dtype=bool) & (language == lang)):
            b = [end for end in old[i] if low[i] <= end <= high[i] + 12800]
            c = [end for end in new[i] if low[i] <= end <= high[i] + 12800]
            if b and c:
                delays.append((c[0] - b[0]) / 16)
        result[lang] = dict(retained=len(delays), signed_ms=delays,
            p95_ms=float(np.percentile(delays, 95)) if delays else None,
            maximum_ms=max(delays) if delays else None)
    return result
