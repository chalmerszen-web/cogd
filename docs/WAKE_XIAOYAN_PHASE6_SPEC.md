# Phase 6: autonomous synthetic-speech improvement

2026-09-22 user direction: do not require human recordings now; use locally
synthesized speech and the nearby speaker to improve the device as far as the
available setup supports. This supersedes the previous field-input blocker for
this development stage. Physical5m and human generalization remain unverified,
but neither is a prerequisite for proceeding with this synthetic experiment.

Keep baseline E/F app and rollback, C11, existing memory/partition budgets and
all Agent functions. No silent threshold changes, relabeling difficult sources,
or reuse of old TEST diagnostic captures for training.

## First experiment J: weak measured mixtures with the existing log-Mel path

Evidence: E/F is better than PCEN G/H; I near-word adaptation regressed weak
Yue. Original log-Mel measured augmentation mainly scales captured signal and
noise together. Explicit fixed-noise weak captured variants were tried only
jointly with PCEN. Test this missing combination without a frontend/runtime change.

- Base features-word-contrast, preserve original TRAIN arrays as exact prefix;
  validation, test and normalization remain byte-identical.
- Original44 TRAIN captures plus41 full-phrase and28 near-word audited captures.
  Never use validation sources as noise or ten TEST diagnostic recordings.
- Derive complete, prefix and suffix examples from positives; retain all existing
  near-word negatives. Add -6/-12dB measured mixtures using another TRAIN
  capture's ambient tail at complementary RMS weight. Match artificial placement
  gaps to original ambient. Same event labels; no boundary or test-gate fitting.
- One J adaptation from Fbest,4000steps,batch32,4threads,lr0.0001,seed20261004.
  Paired+contrast sampling unchanged from I; device fraction0.5. This is a joint
  data/sampling experiment, not an attribution of effect to one isolated variable.
- Evaluate J and one fixed E/J equal-logit fusion with existing three-block
  smoothing. Original validation chooses frozen threshold; weak6/12 scored once.
  No coefficient/member/window/threshold search on weak or TEST examples.
- Deployment gates unchanged from I: original captured8/8per language,0/72
  negatives, overall>=90%per language,<=5%negative clips,zero partial and 小燕
  triggers; weak each-language nonregression,zero added negatives and at least
  two extra Yue hits combined over E/F. Failures retained, never flashed as passes.
- If J fails, diagnose its fixed TRAIN/validation scores before specifying at
  most one additional candidate for this cycle; do not start an indefinite sweep.

If host gates pass: prove C/board parity/resources; application-only guarded
flash; fixed speaker replay at original,-6,-12dB plus a recorded noise window.
Save all source IDs, received PCM, hashes, hits, false events, timing and heaps.
Only a measured improvement can replace E/F. Fresh synthetic held-out voices
and final Agent regression follow a frozen candidate; TTS is not human evidence.

At the cycle boundary, report the best measured configuration and retained
limitations. Do not reinterpret attenuation in dB as actual metres. No user
recording request or field-input block during this authorized synthetic stage.

## Second/final candidate of this cycle K: retain sensitivity, penalize spikes

J's saved best was step1, effectively the old F weights: the mean validation
loss selector rejected the later adaptation. At the fixed step4000 checkpoint,
TRAIN original+weak recall improves to91/96zh and78/90Yue, but36/638negative
clips trigger. Fixed750 E/J-last original captured8/8both,2/72negatives.
This is evidence of a recall/false-event tradeoff, not a passing candidate.

One K run starts at Jlast (not Jbest), same weak corpus/devicefraction0.5,
paired+contrast sampling,2000steps,batch32,4threads,lr0.00005,seed20261005.
Add the already tested0.15 negative top-three-frame loss to reduce short false
peaks. Original-validation selection uses this same weighted loss. Architecture,
feature normalization, positive labels, event windows and runtime unchanged.
Evaluate K and only the fixed E/K fusion, freezing threshold on original
validation then checking weak6/12 once. Same deployment gates as J.
No third automatic training in this cycle, regardless of result. Compare retained
failures and fresh bounded physical replay before publishing the best configuration.

## Temporary E/K comparison after K results

K passes all original host gates, including synthetic小燕: captured8/8both,
0/72negative. At frozen740, weak6zh7/Yue5 (baseline7/4), weak12zh5/Yue1
(baseline6/2), negative0atboth. It is not a passing weak-signal replacement.
The recall/rejection tradeoff justifies one temporary app-only A/B comparison
after exact C/board/resource proof, under the user's autonomous-debug request.
This is an explicit experimental exception to the earlier all-gates-before-
deployment rule; it does not relabel K's failed weak gate as passed.

Freeze20source clips before replay:4positives/language drawn from validation
sources absent from earlier physical selections;12negatives include 小燕,
小王,你好,小言,嗨乐鑫 and ordinary speech in both languages. Run all20at
0/-6/-12dB, so weak false-positive testing includes the known near-word failure
class. Identical sources/gains for E/F and E/K, actual output selected by name.
Fresh physical capture of existing validation voices is not a new blind voice test.

Keep K only if it has no resource/partial-word failures, normal recall does not
decrease, total positive hits across the three levels do not decrease for either
language, total negative triggered clips do not increase and at least one of
those quality counts improves. Otherwise restore verified E/F app and threshold.
Every per-level score remains visible, including regressions. Back up Flash,
update application only; preserve original application and all data partitions.

After live comparison, collect one bounded12-clip validation corpus: first fixed
target,小燕and小言source in each language from the same selection, each at0/-12dB.
Use identical raw microphone PCM in frozen E/F and E/K C libraries to check
whether sequential-session room drift accounts for the difference. Never train
on these captures. Preserve clipping/alignment failures, do not replace sources
according to outcomes. This supports attribution, not live-state equivalence.

## Frozen-candidate fresh synthetic voices

Reserve af_nicole/am_adam from the already pinned Kokoro reference revision,
after verifying neither occurs in the training/source manifests. Generate locally
with the existing pinned CosyVoice Mandarin/Yue engines: two positive pilots
plus one each 小燕/你好/小言 per voice/language (20clips total), carrier sentence
and existing Yue exact-homophone pronunciation recipe. Keep split TEST for both
languages/all descendants. Same TTS family, new reference identities; not humans.

Run existing ASR/boundary curation before any KWS score. Need at least one complete
target per voice/language to interpret voice recall. No model or threshold change
based on this check, no automatic third training/voice search. Retain rejected
audio and any missing-quality combination. Compare old/new frozen recognizers
on accepted sources and finite device replay if the pilot quality gate passes.
