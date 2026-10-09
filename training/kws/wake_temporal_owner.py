"""One causal256ms evidence window; the original detector owns every output."""
from pair_qat import events


def temporal_events(original, verifier):
    if len(original) != len(verifier):
        raise ValueError('Original and verifier stream lengths differ')
    accepted = set(events(original))
    pending = support = None
    votes = 0
    result = []
    for block, score in enumerate(verifier, 1):
        end = block * 512
        votes = ((votes << 1) | (int(score) >= 268)) & 7
        if block >= 64 and votes.bit_count() >= 2:
            support = end
        if pending is not None and end - pending > 4096:
            pending = None
        if end in accepted:
            pending = end
        if pending is not None and support is not None and end - support <= 4096:
            result.append(end)
            pending = None
    return result
