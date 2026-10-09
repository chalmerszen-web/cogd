"""Energy-balanced partial phrases; not a phonetic alignment or a labeler.

Use a verified TRAIN parent's clean PCM as ``reference``. For room recordings,
align that reference with the already audited lag/crop; never choose a cut from
room noise. Positive endpoints and placement metadata remain untouched.
"""
from dataclasses import asdict, dataclass
import hashlib
import math

import numpy as np


VERSION = "clean_energy_quantile_v1"
RATE = 16000
FADE_SAMPLES = 320
CORE_QUANTILES = (.01, .99)
MIN_CORE_SAMPLES = 1280
MIN_ENERGY_FRACTION = .10


@dataclass(frozen=True)
class Cut:
    version: str
    cut: int
    fraction: float
    side: str
    fade: int
    core_start: int
    core_end: int
    removed_reference_energy_fraction: float
    retained_reference_energy_fraction: float
    reference_pcm_sha256: str

    def manifest(self):
        return asdict(self)


def _pcm(value, name):
    value = np.asarray(value)
    if value.ndim != 1 or value.dtype.kind != "i" or value.dtype.itemsize != 2:
        raise ValueError(name + " must be mono PCM16")
    return value


def incomplete(signal, start, end, side, fraction=.5, ambient=None,
               reference=None):
    """Return a fixed-length cut and its clean-reference audit information.

    ``fraction`` selects 40--60% of cumulative energy, not elapsed time.
    A 20ms fade suppresses a click. Require meaningful acoustic material on
    both sides, and fail explicitly for silence or impulsive/ambiguous input.
    This does not certify which syllables remain. Never use its output to
    relabel an existing example automatically.
    """
    signal = _pcm(signal, "signal")
    reference = signal if reference is None else _pcm(reference, "reference")
    if reference.shape != signal.shape:
        raise ValueError("reference must use the same lag/crop as signal")
    if (isinstance(start, (bool, np.bool_)) or
            isinstance(end, (bool, np.bool_)) or
            not isinstance(start, (int, np.integer)) or
            not isinstance(end, (int, np.integer)) or
            not 0 <= start < end <= len(signal)):
        raise ValueError("invalid reference interval")
    if side not in ("prefix", "suffix"):
        raise ValueError("invalid side")
    if isinstance(fraction, (bool, np.bool_)) or not math.isfinite(fraction) or not .4 <= fraction <= .6:
        raise ValueError("fraction outside fixed .4--.6 budget")
    energy = reference[start:end].astype(np.float64) ** 2
    cumulative = np.cumsum(energy)
    total = float(cumulative[-1])
    if total <= 0:
        raise ValueError("silent reference")
    core_start, core_end = (
        start + int(np.searchsorted(cumulative, quantile * total))
        for quantile in CORE_QUANTILES)
    if core_end - core_start < MIN_CORE_SAMPLES:
        raise ValueError("reference energy too concentrated for a phrase cut")
    cut = start + int(np.searchsorted(cumulative, fraction * total))
    if not start < cut < end:
        raise ValueError("cut outside reference interval")
    fade = min(FADE_SAMPLES, cut - start, end - cut)
    mask = np.zeros(len(signal), dtype=np.float64)
    if side == "prefix":
        mask[:cut] = 1
        mask[cut-fade:cut] = np.linspace(1, 0, fade)
    else:
        mask[cut:] = 1
        mask[cut:cut+fade] = np.linspace(0, 1, fade)
    # Audit speech energy before adding ambient. Background is not evidence
    # that any meaningful part of the target utterance was removed.
    retained = float(np.sum(energy * mask[start:end] ** 2) / total)
    if not MIN_ENERGY_FRACTION <= retained <= 1-MIN_ENERGY_FRACTION:
        raise ValueError("cut does not retain/remove enough reference energy")
    if ambient is None:
        background = 0
    else:
        ambient = _pcm(ambient, "ambient")
        if not len(ambient):
            raise ValueError("empty ambient")
        background = np.resize(ambient, len(signal)).astype(np.float64)
    result = np.clip(np.rint(signal.astype(np.float64) * mask +
                             background * (1-mask)), -32768, 32767).astype(np.int16)
    audit = Cut(VERSION, cut, float(fraction), side, fade, core_start, core_end,
                1-retained, retained,
                hashlib.sha256(reference.astype("<i2").tobytes()).hexdigest())
    return result, audit
