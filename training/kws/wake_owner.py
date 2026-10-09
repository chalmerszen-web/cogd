"""Same-timestamp original-owned verification; no future window or new cooldown."""
from pair_qat import events


def owner_events(original, verifier):
    if len(original) != len(verifier):
        raise ValueError('Original and verifier stream lengths differ')
    accepted = set(events(original))
    votes = 0
    result = []
    for block, score in enumerate(verifier, 1):
        votes = ((votes << 1) | (int(score) >= 268)) & 7
        end = block * 512
        if end in accepted and votes.bit_count() >= 2:
            result.append(end)
    return result
