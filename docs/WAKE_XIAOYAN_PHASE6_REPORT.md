# Phase6 synthetic-speech optimization (in progress)

The user explicitly deferred human recordings on2026-09-22. This stage uses
local synthetic speech, physical speaker replay and the existing C11 Agent.
Actual5m and human generalization are not claimed or required to continue here.

## Training evidence

474new weak measured TRAIN mixtures added to14360old examples, total14834.
Sources:44original,41audited full-phrase and28audited near-word captures. Only
TRAIN ambient mixed; old arrays exact prefix, validation/TEST/normalization
byte-identical,zero clipped added samples. `features-weak-logmel/features.json`
retains ancestry and hashes; all artifacts below are in `artifacts/kws-phase5`.

J:4000steps44.759s, F initialization, devicefraction0.5, contrast sampling.
Mean validation loss chose step1. Fixed TRAIN float diagnosis824examples shows
J-best barely differs from F; J-last improves recall but adds false events.
J-last fixed threshold355:zh91/96,Yue78/90,negative36/638,premature4. This is
in-sample diagnosis, not generalization. See `adapt-j-diagnosis/`.

K:2000steps22.837s, fromJ-last, same data with0.15negative peak loss,lr0.00005.
No third training launched. Models J/K and their thresholds/checkpoints are
retained separately; old failures remain unchanged.

| Frozen pair | Captured original zh/Yue | Original negative clips | Synthetic 小燕 | Weak6 zh/Yue | Weak12 zh/Yue |
|---|---|---|---|---|---|
| E/F,750 | 8/8,8/8 | 0/72 | failing | 7/8,4/8 | 6/8,2/8 |
| E/J,750 | 8/8,8/8 | 1/72 | failing | 7/8,4/8 | 6/8,1/8 |
| E/K,740 | 8/8,8/8 | 0/72 | 0 triggers | 7/8,5/8 | 5/8,1/8 |

Weak mixtures have0/72negative clips for both new pairs at each level. E/K passes
all original host gates but fails weak nonregression; it is only a temporary
physical-comparison candidate. Its better near-word rejection must be weighed
against the weak recall losses, not called a universal improvement.

## Numerical and runtime preparation

Each E/K member4096feature frames and512PCM frames matches exported integer
oracles/C. Shared frontend512frames and256/512sample API paths are exact;
workspace12632B. C ASan/UBSan2/2pass,49Python tests pass4.27s. Evidence:
`fusion-ek-smooth3/{adapt-e-parity,adapt-k-parity,fusion-host-parity.json}`.
K uses separate generated source/build; default E/F source remains unchanged.
E/K application 1315360 bytes, SHA256
989f5fb65f6c0daac98ff05a66785b8a12c34fad5705e5d05b412e83cce92154,
was backed up/flashed with non-application byte identity. Board parity:
512 frames,176640 values,zero mismatches,max256-sample step5075us.

## Physical comparison protocol

`artifacts/kws-phase6/replay-selection.jsonl` freezes20sources before replay:
four fresh physical positive sources per language and twelve negative examples.
The sources are already-used development validation voices; this is not blind
voice testing. All20are replayed at0/-6/-12dB, including 小燕 at every level.

Selection initially stopped before any hardware action because only one unseen
Yue example remained in one voice group. Replaced the equal-two-per-voice
assumption with deterministic round-robin allocation over remaining sources;
both voices retained, no score-based selection or sample relabeling.

Both60-trial runs completed. E/F:zh10/12,Yue9/12,negative6/36. E/K:
zh10/12,Yue10/12,negative4/36. Both hadzero partial-word errors and no
DMA/reset/resource failures, maximum observed inference8156/8126us respectively.
K satisfies the preregistered finite-batch selection rule and was retained.
Close-range 小燕 false triggers remain; this is not a general recognition rate.

The12-clip same-PCM validation capture also completed. Both pairs score
3/4positive and2/8negative triggered on those identical recordings. This small
control does not establish a general K improvement; sequential room variation
can contribute to the60-trial difference. Evidence is in
`artifacts/kws-phase6/same-pcm-comparison.json` and `selection-decision.json`.

The reserved two fresh synthetic voice references downloaded, but their pilot
generation/test remains unexecuted. The subsequent user goal now prioritizes
the full device ASR/DeepSeek/TTS conversation; no third training has been run.
This report does not mark synthetic generalization, actual5m or human testing
as passed. Current integration requirements are in `VOICE_CLOUD_SPEC.md`.
