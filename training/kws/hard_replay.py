"""Finite replay of diagnosed TRAIN windows, capped per recording and label.

The regular sampler and its RNG are untouched. Related channel/cut variants
share a recording key; positive and negative roles have separate budgets.
This host helper neither changes labels nor participates in firmware builds.
"""
import heapq
import numpy as np


def recording_families(clip_ids):
    clips = np.asarray(clip_ids)
    if clips.ndim != 1 or clips.dtype.kind not in 'US':
        raise ValueError('Expected one-dimensional string clip identities')
    result = []
    for clip in clips.astype(str):
        if not clip:
            raise ValueError('Empty clip identity')
        for prefix in ('channel-', 'weak-logmel-'):
            if clip.startswith(prefix):
                clip = clip[len(prefix):]
        for suffix in ('-prefix', '-suffix'):
            if clip.endswith(suffix):
                clip = clip[:-len(suffix)]
        if not clip:
            raise ValueError('Empty recording identity after known adapters')
        result.append(clip)
    return np.asarray(result)


def source_capped_schedule(families, labels, selected, base_counts, existing_counts, *, steps, target, seed):
    families = np.asarray(families)
    labels, selected, base_counts, existing_counts = map(np.asarray, (labels, selected, base_counts, existing_counts))
    if families.ndim != 1 or families.dtype.kind not in 'US' or not len(families) or np.any(families == ''):
        raise ValueError('Expected nonempty recording identities')
    n = len(families)
    if labels.shape != (n,) or not np.all((labels == 0) | (labels == 1)):
        raise ValueError('Expected binary labels matching recordings')
    for counts in (base_counts, existing_counts):
        if counts.shape != (n,) or counts.dtype.kind not in 'iu' or np.any(counts < 0):
            raise ValueError('Expected nonnegative integer exposure counts')
    if selected.ndim != 1 or not len(selected) or selected.dtype.kind not in 'iu':
        raise ValueError('Expected nonempty integer diagnosed indices')
    if np.any(selected < 0) or np.any(selected >= n) or len(np.unique(selected)) != len(selected):
        raise ValueError('Diagnosed indices duplicate or out of range')
    if type(steps) is not int or type(target) is not int or steps < 2 or target < 1:
        raise ValueError('Positive finite step and target budgets required')
    rng = np.random.default_rng(seed)
    keys = sorted({(str(families[index]), int(labels[index])) for index in selected})
    heaps, remaining, rows = {}, {}, {}
    exposure = base_counts.astype(np.int64)+existing_counts.astype(np.int64)
    for family, label in keys:
        key = family, label
        members = np.flatnonzero((families == family) & (labels == label))
        diagnosed = np.sort(selected[(families[selected] == family) & (labels[selected] == label)])
        base, prior = (int(counts[members].sum()) for counts in (base_counts, existing_counts))
        deficit = max(0, target-base-prior)
        remaining[key] = deficit
        tie = rng.permutation(len(diagnosed))
        heaps[key] = [(int(exposure[index]), int(tie[j]), int(index)) for j, index in enumerate(diagnosed)]
        heapq.heapify(heaps[key])
        rows[key] = dict(recording=family, label=label, members=members.tolist(), diagnosed=diagnosed.tolist(),
            base_draws=base, existing_extra_draws=prior, target_draws=target, extra_draws=deficit,
            final_source_role_draws=base+prior+deficit, extra_by_window={str(index): 0 for index in diagnosed})
    total = sum(remaining.values())
    if total >= steps:
        raise ValueError('Replay requires more than one additional row per step')
    order = [keys[int(index)] for index in rng.permutation(len(keys))]
    draws = []
    while any(remaining.values()):
        for key in order:
            if not remaining[key]:
                continue
            count, tie, index = heapq.heappop(heaps[key])
            heapq.heappush(heaps[key], (count+1, tie, index))
            rows[key]['extra_by_window'][str(index)] += 1
            remaining[key] -= 1
            draws.append((key, index))
    schedule = [dict(step=(j+1)*steps//(total+1), index=index, recording=key[0], label=key[1])
        for j, (key, index) in enumerate(draws)]
    assert len({row['step'] for row in schedule}) == total
    assert all(1 <= row['step'] <= steps for row in schedule)
    return dict(complete=True, base_steps=steps, original_sampler_unmodified=True,
        target_draws_per_recording_and_label=target, extra_draws=total,
        negative_draws=sum(row['label'] == 0 for row in schedule),
        positive_draws=sum(row['label'] == 1 for row in schedule),
        families=[rows[key] for key in keys], schedule=schedule,
        same_seed_no_seed_search=True, one_extra_row_per_step=True,
        scope='Canonical recording variants; positive and negative role caps counted separately, not an independent-source/generalization claim.')
