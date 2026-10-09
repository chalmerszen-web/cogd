# Phase5 development results — not final acceptance

Updated 2026-09-21. Goal: Mandarin and Cantonese "你好，小言", followed by
weak-signal testing and an actual five-metre speaker-to-device test. Neither
five-metre performance nor independent human-speaker generalization is verified.

## Current device

Last USB readback: `0.7.0-xiaoyan-exp`, `xiaoyan_ds_tcn24_ef3`, threshold750,
gain1, listening disabled between tests. Latest standalone-PCEN-diagnostic USB
snapshot confirms free heap102096B,min heap75236B after a new boot,context251252B within unchanged2MiB partition
and128KiB prompt budget. No playback/capture remains active; COM5 released.
This is the experimental fixed E/F
fusion with a causal three-block logit mean. Application1315248B; SHA256
`8bf128136a75825132ae50c91e596ecbc2efda175330c15b9096ed86c8decafc`.
The latest752B addition is a separate USB PCEN diagnostic; PCEN does not feed
the active recognizer. Acoustic and full Agent regression above ran on the
previous appSHAf753ab5458fd79da40f261595b6b40487a2295cab340814ac407a307438e0970;
current app separately passed diagnostic CRC/timing, listener resume and context
preservation. Do not relabel old tests as fresh tests of the latest binary.
No partition/context-capacity change. Current status evidence:
`artifacts/kws-phase5/pcen-prototype/board-check.json`.

The verified application-only install preserved non-app bytes before boot.
Latest full backup: `backups/kws-phase5-20260921-140828`; SHA256
`4a5f869c37b70f5eda6aa4b7af39e67be2d85d04d3e209ff2f6c015034110e1b`.
Legacy0.6.3 rollback remains under `firmware/rollback/0.6.3-context` and source
under `history/0.6.3-context`. C and D development never authorize erasing data.

## Data and training

Collected76 physical board recordings, preserving source voice grouping:
64initial plus12Mandarin negative supplement. Rejected4 low-alignment TRAIN
recordings, retained all VALIDATION recordings with explicit uncertainty.
The72retained recordings include44TRAIN and28VALIDATION. Both are synthetic
speaker replay, not independent human speech. Full audits are in
`artifacts/kws-phase5/features/features.json` and both corpus reports.

Training/noise response fitting uses TRAIN captures only. Normalization stays
identical to Phase3. No general inference runtime was added: original24channel
C11 network,6673parameters per member. Single-member workspace is9448B;
the installed fusion shares the frontend and uses12632B total workspace.

| Candidate | Change | Budget | Device-recording validation | Deployment |
|---|---|---|---|---|
| A | Captured-domain fine-tune | 3000steps | zh5/8,yue3/8;negative1/40 | Not flashed |
| B | Measured TRAIN spectral response and ambient noise | 6000steps | zh8/8,yue6/8;negative0/40 | Previous physical experiment; backed up |
| C | Paired incomplete prefixes/suffixes,20% sampling | 4000steps | zh8/8,yue7/8;originalnegative1/40;paired4/32 | Failed gate, not flashed |
| D | Extra penalty on three strongest negative frames | 3000steps | zh8/8,yue5/8;originalnegative0/40;paired2/32 | Failed gate, not flashed |
| E | Additional audited physical TRAIN data, C settings | 4000steps | zh8/8,yue7/8;originalnegative0/40;paired2/32 | Failed gate, not flashed |
| F | Fresh joint-domain initialization | 6000steps | zh7/8,yue7/8;originalnegative0/40;paired1/32 | Failed standalone gate |
| E/F fusion | Equal logits and causal three-block mean | No further training | zh8/8,yue8/8;originalnegative0/40;paired0/32 | Installed for physical experiment only |

C creates3960incomplete examples, with source ancestry retained. Time cuts do
not claim phonetic boundaries. All original validation arrays remain exactly
the same prefix, and the test file is unchanged. C frozen threshold770; no
heldout-test evaluation. Its original standalone partial-word validation is
0/11false triggers overall (0/3captured), but derived captured partial phrases
still trigger4/32. Float also fails; this is not solely quantization degradation.

Diagnostic-only C threshold sweep shows950still triggers one derived partial
phrase and reduces captured Yue recall to3/8. Operating threshold unchanged.
Sparse negative peaks contribute little to the128-frame average loss; D tests
that hypothesis without changing device code or architecture. See frozen
`adapt-c-comparison.json` and `adapt-c-negative-loss-diagnosis.json`.

D at frozen820reduces false triggers but loses three captured Yue examples;
its original all-domain Yue recall28/33also fails90%. ModelJSON SHA256
`4dc8c5d7622f9b34eb1915a78a569635ab24a39d3596296363670cb658fbf5a9`.
It is not an accepted improvement and was not flashed or scored on test data.
TRAIN-only float diagnostic selected186examples independently of scores:
C/D captured positives22/22, captured derived negatives0/24false triggers.
This supports investigating generalization/data coverage before increasing
network capacity. In-sample scores are not acceptance evidence.

Next collection is fixed48TRAIN-only captures from24previously unused source
utterances, baseline and-12dB, including full phrases and standalone halves.
The selection/plan is frozen under `full-phrase-supplement-*`. Collection is
complete48/48. Following quality inspection,41were eligible (zh19,Yue22), with
all ADC-clipped and unreliable-alignment examples excluded. Bounded GCC-PHAT
recovered11weak alignments where RMS envelopes failed. Known-delay and random
unrelated-waveform tests passed. Same-phrase/different-source controls produce
3/48matches, all partial phrases: this is not a source-identity detector.
Local ASR screening of8fixed recordings finds full speech at both levels but
some homophone/near-spelling outputs, not human pronunciation verification.

E adds326TRAIN examples with measured signal, paired incomplete phrases,
background and bounded speed variants. Validation/test/normalization remain
byte-identical. First preparation failed on Windows absolute paths in WSL
before any training; preserve failed log, fixed source and `features-measured-v2`.
One4000step E run finished42.67s, frozen880; recorded-positive recall unchanged
versus C, original recorded negatives improve1/40to0/40, derived recorded
partials improve4/32to2/32. This is development-validation improvement, not a
new blind or physical-distance result. E fails the full-phrase deployment gate;
no application was flashed and no E test-source campaign ran.

## B physical results

Endpoint0: Misiom-Shooter, last Windows readback100%,unmuted. Baseline source
RMS0.14 and playbackgain0.35. These are digital settings, not a calibrated SPL.
Every scored negative includes at least1200ms after playback ends. Source
selection, audio-file hashes and executed script snapshots are retained.

| Test | Mandarin valid | Cantonese valid | Triggered negative clips |
|---|---:|---:|---:|
| Near-field | 18/20 | 18/20 | 5/20 |
| Relative -6dB | 3/4 | 4/4 | 0/10 |
| Relative -12dB | 3/4 | 1/4 | 1/10 |

Near Cantonese had20triggers but only18within the accepted endpoint interval;
one was early and one late. Near false triggers: Yue小言, Mandarin小言, Yue你好,
Yue你好小燕, Mandarin你好小燕. B therefore fails full-phrase acceptance.
Small weak subsets are diagnostics, not precise population rate estimates.
Repeated test-source playback is regression evidence, not a newly blind test.
Phase4 and5 playback/room conditions were not fully controlled across days;
do not claim a paired acoustic improvement percentage against Phase4.

Resource checks:512board parity frames exact; near maximum5188us,P99<=5500us;
weak maxima5151/5168us. No resource/DMA/reset failures in these runs. This does
not replace the remaining full Agent regression or longer background run.

Raw microphone meter values, saved with age and wake state, are in weak run
reports. Peak observed speech-window/noise ratios are sometimes nonmonotonic
between attenuation levels and truncate when listening ends. They are not
calibrated SNR, source SPL or physical distance.

## E/F fusion installed experiment

Frozen equal integer logits followed by a causal three-block mean at750permille
passed development validation: zh45/45,Yue33/33; negative5/636, paired0/156,
standalone partial0/11. Captured subset8/8each and0/72negatives. These are reused
development sources, not independent generalization proof. No endpoint changed.

Each member4096feature/512PCMframes matches its integer oracle. Fusion512PCM
frames and256/512sample feeding match; allocation-free sanitizer tests and
36host tests passed. C3 parity512frames has zero mismatches for primary trace,
both logits, smoothed score and event; maximum6125us per256sample diagnostic.
Live512sample smoke maximum8073us,P99<=8500us, no resource failures. Smoke
validation-source replay: zh2/2,Yue2/2,partial negatives0/4.

| Physical test | Mandarin valid | Cantonese valid | Triggered negatives |
|---|---:|---:|---:|
| Near-field | 20/20 | 20/20 | 2/20 |
| Relative -6dB | 4/4 | 4/4 | 0/10 |
| Relative -12dB | 4/4 | 1/4 | 0/10 |

All seven near-field standalone partial phrases were rejected. Both remaining
false triggers are "你好小燕", one per language. Thus negative10% fails the
registered5% gate. At-12dB, Yue25% also fails; neither result is accepted as5m
capability. The smaller weak subset does not include the two failing小燕 clips,
so zero weak false triggers does not prove those confusions resolved. Compared
with B on identical hashed source files, near recall rises18/20to20/20each and
negative clips fall5to2; sessions were sequential with uncontrolled ambient.
Do not interpret this as a controlled population improvement estimate.

The new runs retain >=1200ms post-playback observation per clip. B's near report
predates that explicit timing field; its tail is not retroactively certified by
the new summarizer. New formal/weak maximum8149/8075/8125us,P99<=8500us,
no resource failures. Machine-readable comparison: `acoustic-comparison.json`.

Frozen host test scoring uses the same exact recipe (first reproduced saved
validation scores), no threshold search. INT8 zh231/234,Yue150/155,negatives4/85,
partial0/7; floating recalls identical. One premature event remains. These
previously used Phase3 reserved synthetic sources are regression evidence,
not a newly blind evaluation. See `frozen-test/report.json`.

Agent regression passed real DeepSeek streaming/nonstreaming with required
GPIO/control tools, USB status, light restoration, short score playback,
microphone start/stop, listener suspend/resume and context queries.
LCD protocol/cancel/reopen regression passes (cancel406ms), without a visual
pixel claim. Twenty wake reopen cycles pass, listening free heap74328..74468B;
idle diagnostic session expires30.0s and releases resources. Two-second capture
and replay pass; final free heap101216B,min heap64172B. Context unchanged by
LCD checks, two real LLM conversations append normally. Evidence:
`agent-regression`, `display-regression.json`, `extra-device-checks.json`,
`post-regression-status.json`.

Ten diagnostic TEST-source captures are fixed before acquisition: four-12dB
Yue cases, two weak Mandarin hit controls, two matching normal Yue cases and
both near-word false triggers. TEST ancestry is retained; these recordings are
excluded from training. Separate capture mode enforces10maximum and TEST-only
selection; default capture still rejects TEST data. Three guard tests pass.
Initial guard-test collection lacked serial in the ML environment; hardware
imports were moved into main and the three tests then passed. This does not
change acquisition behavior. Acquisition completed10/10, all ADC clipping
counters0. Separate captures do not reconstruct the exact original live trial.

Nine of ten recordings align reliably; the first weak Yue capture does not,
so no timing-correct hit rate is asserted for it. Three aligned weak Yue clips
have speech-plus-noise versus initial-background power contrast1.64..3.44dB;
matched normal-level Yue4.45/4.84dB. These are not calibrated SNR estimates;
one Mandarin comparison is negative, showing room/noise nonstationarity.
Frozen C models replay each capture after2.048s repeated own ambient prefill.
Fixed digital gain1/2/4 does not consistently restore weak positives; gain4
clips some samples in every diagnostic waveform. Both near-word negatives
still fire atgain1/2. No firmware gain or threshold changed. Local SenseVoice
screens all10captures: weak Yue often omits/confuses the final name syllable;
normal Yue uses homophone spelling; both小燕 negatives transcribe as小燕.
Automatic ASR is neither human pronunciation proof nor a causal diagnosis.

Prior ADC12/6/12A/B/A in ACTIONLOG found no demonstrated recognition benefit;
do not repeat it or blindly increase gain. Next development should compare a
bounded noise-robust frontend/data treatment on TRAIN/VALIDATION sources before
any more flashing. Retain these TEST captures for diagnosis only, never training.
Deferred-18/-24dB repetitions and long-background acceptance until the observed
weak-Yue and near-word failures are addressed; the original5m requirement stays.
Evidence: `capture-diagnosis.json`, `diagnostic-asr.json` in `fusion-ef-smooth3`.

## Rejected subtraction and separately trained frontend route

One frozen mild magnitude-subtraction pilot reconstructed88captured validation
waveforms with exact original C features and E/F scores. It was evaluated on
those originals and two fixed approximate weaker mixtures,264waveforms total.
Original zh/Yue8/8each became7/8each with2newnegative triggers. At-6/-12dB
perturbations, Yue4/8and2/8 became2/8and1/8. Reject this drop-in processor;
no grid, training, threshold change, TEST data or device replay in that pilot.
Evidence: `noise-frontend-pilot/report.json`.

Added a standalone fixed-point PCEN prototype using the dynamic-compression
approach described in [Wang et al.](https://arxiv.org/abs/1607.05666). Parameters
alpha1,delta2,r1/2,epsilon1magnitude unit,smoothing1/32; requires new training
and normalization. Current model cannot use it unchanged. PureC11,160Bstate,
no floats/heap allocations. Independent Python integer oracle agrees on
4096frames/163840values; maximum floating-formula error0.981254Q8unit on the
same rounded magnitude/EMA. ASan/UBSan2/2suites and41Python tests pass.

Reserved-session diagnostic runs256adversarial frames on the C3. Two runs
match hostCRC1168469754; maxima213/285us per40band frame, total33890/34093us,
command46/47ms,heap86860Bbefore/after. CRC is a fingerprint comparison, not
an export of every device intermediate. C3 stack report forPCENstep32B;
state remains on diagnostic stack. Live E/F workspace12632B unchanged and
listener resumes,8blocks max8078us. No detection-quality claim from this test.
Evidence: `pcen-prototype/parity.json`, `board-check.json`, `flash.log`.

### Candidate G: completed and rejected

Exact Mel-power extraction now feeds an explicit experimental PCEN frontend.
Default log-Mel512PCMframes remain equal to the old host library;512PCENframes
match an independent integer oracle and reset deterministically. This full
frontend integration is host-only; the installed app has the standalone
PCEN diagnostic described above, not PCEN recognition. Host evidence:
`pcen-prototype/full-frontend.json`.

Prepared11191TRAIN/714original-validation examples, plus88examples each for
fixed-6/-12dB mixtures. Source identities remain disjoint; all original714
validation IDs/labels/endpoints match the prior data. Normalization uses TRAIN
only. No TEST WAVs or ten diagnostic captures used. This changes frontend and
augmentation jointly and cannot isolate PCEN's causal effect. Evidence:
`pcen-g-features/features.json`, `audit.json`, `captured-input-audit.json`.

One6000step fresh run completed62.999s, selected step5100 by validation loss.
Calibration uses TRAIN; original validation froze560permille/Q862. Source
freeze verified after completion. ModelJSON SHA256
`c2be06616e27734a29b5056bceb6caf2e290afcb2e9f27cd429d16f880810d9a`;
checkpoint SHA256
`f042d5bf658b99172c79115dad20a996e8d9054e3ccda921da1df5e140d8a9dd`.

| Captured development condition | E/F zh / Yue hits, each out of8 | G zh / Yue hits | E/F negative clips /72 | G negative clips /72 |
|---|---|---|---|---|
| Original | 8 / 8 | 7 / 7 | 0 | 5 |
| Approximate-6dB mixture | 7 / 4 | 7 / 4 | 0 | 6 |
| Approximate-12dB mixture | 6 / 2 | 6 / 3 | 0 | 12 |

G all-domain validation zh43/45,Yue31/33,negative30/636; standalonepartial0/11.
It fails captured recall, captured negatives, weak negatives and required Yue
improvement gates. Reject device activation; no second training or TEST scoring.
These mixtures are offline perturbations of existing recordings, not fresh
speaker/device replay and not measured metres. Evidence:
`pcen-g-int8/validation-metrics.json`, `weak-validation.json`.

Read-only diagnosis selects all268unattenuated recorded TRAIN examples before
scoring: positives25/25zh,24/24Yue,negative8/219INT8 and5/219float. Original
captured validation float is7/8zh,6/8Yue,negative3/72, including two prefix cuts
and one ambient example. Thus quantization is not the sole failure; training
data also retains negative errors. Across the fixed500..950permille threshold
grid, neither float nor INT8 can achieve full captured recall with zero false
and premature events. This envelope does not change the frozen threshold.
Input clipping is below0.57% of evaluated positive feature values; float/INT8
logit errors are recorded separately. No causal assertion that clipping,
capacity or PCEN alone explains the failure. Evidence: `pcen-g-int8/diagnosis.json`.

Source review confirms the kokoro-named groups refer to reference identities;
the actual bilingual generator is local CosyVoice2, as recorded in Phase3 and
the source manifests. This does not establish correct human pronunciation for
every clip; it also does not justify calling them Mandarin-only Kokoro output.

Full Python regression exposed a missing `kws_pcen.c` in the export-test link
list after frontend refactoring. Fixed;41tests pass. Final read-only USB query:
E/F3,750,listeneroff,freeheap102136,minheap75236. No application flash during
G training/diagnosis. Actual5m and acoustic acceptance remain incomplete.

## Candidate I: measured contrast adaptation, still rejected

Appended190examples from28audited recordings to the14170existingTRAINexamples;
all old arrays remain an exact prefix. Validation/test/normalization bytes are
unchanged, speaker groups separate. One2000step adaptation from Fbest completed
22.773s with fixed contrast sampling;44hosttests pass. E remains frozen, E/I
equal-logit3blockmean is the only pairing evaluated. Original validation froze
830permille/Q8406. Captured zh8/8,Yue8/8,negative0/72, but one synthetic Yue
你好小燕validation source still triggers; the registered near-word gate fails.

Frozen weak-mixture diagnosis reconstructs all88original captured validation
features exactly. -6dBzh6/8,Yue2/8,negative0/72; -12dBzh4/8,Yue0/8,negative0/72.
This regresses weak recall against E/F and is not a deployable improvement.
No threshold adjustment, second I training, TEST evaluation or device flash.
Evidence: `features-word-contrast/audit.json`, `adapt-i-finished.json`,
`fusion-ei-smooth3/{validation-metrics,weak-validation}.json`.

Source diagnosis inspects21development clips only with local librosa0.10.2
[pYIN](https://librosa.org/doc/0.10.2/generated/librosa.pyin.html).120/240Hz
controls return119.90/239.80Hz. Six clips have insufficient high-confidence
tail pitch. Failing negative's last350ms trajectory overlaps some same-voice
positives, but this window is not forced syllable alignment and pitch alone
cannot determine the word/tone. Its historical ASR text says小演; that also is
not phonetic ground truth. Labels/thresholds unchanged, hard example retained.
Evidence: `yue-pitch-diagnosis/report.json`, `comparison.png`, fullF0arrays.

## Required external field evidence

The synthetic-source campaign has not proven natural bilingual pronunciation
or five-metre pickup. Current arrangement is near the speaker; software cannot
move the speaker/device or independently confirm a human speaking distance.
Further acceptance requires independent human near/5m recordings and reliable
review of target versus near-word pronunciation. This is an external evidence
dependency, not an assertion that a further untested model cannot improve.
No more automatic same-corpus training is queued after the failed bounded I run.

Prepared `采集真人唤醒样本.cmd` and `capture_human_review.py`; user-operated
language/distance entry,10second positioning delay,green recording cue,local
PCM/hash/ADC/source-confirmation records,priorclipslotexport and bounded cleanup.
Defaults3positive/3near-word recordings per language, a diagnostic pilot only.
Help path and Git exclusion checked; actual human interaction remains unrun.
No recording started by preparing this entry point. See
[field-review instructions](WAKE_XIAOYAN_FIELD_REVIEW.md). Active E/F remains.

## Evidence and remaining gates

### Capacity check and H adaptation

G's268unattenuated recorded TRAIN examples were fitted in one small-set
diagnostic:1500steps,seed20261001,fresh24channel model,originalGfloatthreshold295.
At final step zh25/25,Yue24/24,negative0/219,premature0. This checks trainability,
not generalization; validation/test were not evaluated. The conditional48channel
comparison was skipped by rule. No device capacity increase is justified by
this evidence. Diagnostic checkpoint cannot be used as a release candidate.
Evidence: `capacity-probe/report.json`, source/input freeze and saved scores.

One H adaptation from Gbest (not the diagnostic checkpoint) completed2000steps
in22.027s. Same architecture/features/normalization,device sampling0.75,
lr0.0001,seed20261002. Original validation selected660permille/Q8170.
Captured original zh7/8,Yue7/8,negative4/72; weak6zh7/8,Yue3/8,negative3/72;
weak12zh6/8,Yue5/8,negative13/72. Original float stillzh7/8,Yue6/8,negative3/72.
H also fails the registered gates; no flash or TEST evaluation, no automatic
second adaptation. Close the current PCEN candidate branch rather than rerun
the same data/threshold search. Evidence: `pcen-h-int8/weak-validation.json`;
modelJSONSHA256`dc2b6905ac429ece192866c7fa2f5ec06edd4c16b7c3378a6bbbcbdf9fb94573`.

Metadata coverage reveals7syntheticTRAIN你好小燕negatives per language but zero
recordedTRAIN小燕negatives; recorded near-word negatives were小王. Registered
32TRAIN-only contrast captures, four TRAINvoices, both languages, fullword and
小燕, normal/-12dB. Source selection is frozen before acoustic/model scores;
these are new microphone recordings of development sources, not blind data.
Capture/audit completion is reported separately; no new training auto-start.

### New physical TRAIN contrast corpus and frozen baseline

Capture8893completed32/32, sameE/Ffirmware, namedMisiom-Shooter output, gains
0.35and0.087916. Previous local recording exported before collection. Quality
audit accepts28: three ADC-clipped records and one weak-Yue alignment failure
excluded, without replay or relaxed gates. Each language/level has7eligible.
Original PCM/source hashes verified. Evidence: `corpus-word-contrast/report.json`,
`word-contrast-selection.json`, `word-contrast-plan.json`, `word-contrast-audit.json`.

One frozen E/F offline baseline on these28raw device recordings uses the verified
host library,threshold750/Q8281,2.048s repeated initial ambient prefill and the
existing event/endpoint rule. It is not the exact prior live listening state.

| Captured TRAIN group | Fullword valid hits | 小燕 negative clips triggering |
|---|---|---|
| zh normal | 4/4 | 2/3 |
| zh -12dB | 1/3 | 2/4 |
| Yue normal | 2/3 | 3/4 |
| Yue -12dB | 0/3 | 0/4 |

No premature events in this baseline. WeakYue zero false events accompanies
zero true hits and is not a quality improvement. These new TRAIN recordings
provide concrete acoustic counterexamples before adaptation; they are not
independent human or TEST evidence. This baseline preceded candidate I, whose
subsequent failed adaptation is reported above. Evidence:
`word-contrast-baseline.json`. Original validation boundaries and PCEN/I failures
remain retained. Actual5m remains unverified.

- Physical: `near-smoke`, `near-formal`, `weak-minus06`, `weak-minus12`,
  `weak-results.json`, `weak-received-levels.json` under `artifacts/kws-phase5`.
- Host/C/board exactness: `host-parity`, `board-parity` (candidate B).
- Training: separate immutable `adapt-a`, `adapt-b`, `adapt-c` and INT8 bundles;
  commands and source hashes recorded by each orchestrator.
- Host tests:44passed before the human capture lifecycle checks, including fusion, diagnostic-source guards, host-width streaming and TRAINcontrast selection. D is an experiment,
  not a claim that its objective fixes acoustic recognition.
- Human capture lifecycle: five isolated simulated-device checks passed, covering
  success, port-open failure, ADC readiness timeout, interruption before speaker
  confirmation and close failure. These are not physical/human acceptance tests.
  Stable anonymous speaker groups now span languages/distances; pending captures
  survive cancellation. Latest read-only COM5 snapshot is
  `field-entry-check/20260921-232412/status.json`: E/F3, threshold750, wakeoff,
  no recording, freeheap102140B. Serial port released; no new flash.
- Still required: near-word false-trigger gates, robust low-level Yue,
  remaining full-length/concurrent Agent regression and background observation,
  final real5m placement/source-level evidence and user verification.

Do not mark the active goal complete from this report.
