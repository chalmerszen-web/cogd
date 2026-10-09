# Phase5: device-domain adaptation and five-metre wake objective

Authorized 2026-09-21: implement Mandarin + Cantonese "你好，小言" waking,
then reduce nearby-speaker playback level to exercise weak signals, aiming at
5 metres. Keep the complete Agent and existing partitions/context/recording.

The goal is working recognition, not merely running an experiment. Phase4's
failure exit does not close this new goal. Do not report 5m achieved from gain
reduction alone: actual room distance, reflections/noise and source level must
be verified in a final physical test. An uncalibrated level is labelled dB
attenuation relative to the baseline, never metres.

Initial bounded development cycle:
1. Revalidate board/rollback, diagnose gain and spectral/timing differences on
   training-source captures already recorded. Keep Phase3/4 evidence immutable.
2. Collect source-labelled board recordings from TRAIN and VALIDATION voices
   separately, preserving source ancestry, speaker grouping and actual timing.
   Collect a bounded noise window. No private audio upload. Capture only while
   testing and retain recordings locally.
3. Build device-domain augmentation from training captures only. Separate
   evaluation by original voice/source. At most two initial 6000-step training
   candidates; inspect measurable validation differences before another cycle.
4. Quantize/export, prove C/host/board parity and C3 resource limits before
   deployment. Preserve old app and use app-only guarded flashing.
5. Freeze one model/threshold and test both languages, partial/near/old words,
   speech/noise/music. Near-field target >=90% per language and <=5% triggered
   negative clips with no partial-word triggers in the fixed experiment.
6. Test fixed attenuation tiers 0,-6,-12,-18,-24dB, recording received level,
   noise/SNR, clipping, recall and false triggers. Label them weak-signal tests.
   Add room-response/noise robustness experiments; preserve failures.
7. Verify five-metre real placement at a stated source level and background,
   with both languages, before claiming the full objective complete. Actual
   device/speaker placement requires physical access; defer this input request
   until near-field is functional and tests are ready.

Runtime limits: app<=1540096B, KWS workspace target<=20KiB, min heap>=32KiB,
largest listening block>=24KiB, block maximum<32ms/P99<=16ms, no DMA loss/reset.
Complete USB/WiFi/DeepSeek/light/LCD/music/recording/context regression before
delivering an accepted application. No full erase/eFuse writes or data shrink.

Track commands, actual budgets and evidence in ACTIONLOG.md and artifacts/kws-phase5.
Room-response rationale: https://pyroomacoustics.readthedocs.io/en/stable/pyroomacoustics.room.html
and https://github.com/dscripka/openWakeWord (noise and RIR far-field testing).

## Second bounded development cycle: full-phrase discrimination

Candidate B physical near-field result: valid18/20 each language, negative5/20,
including standalone partial phrases. Weak -6dB zh3/4,yue4/4; -12dB zh3/4,yue1/4.
Neither candidate is an accepted solution. After these measured initial A/B
results, run one additional candidate C, fixed4000steps/batch32/lr0.0001,
seed20260925, initialized from B. No network, frontend, partition or workspace
change. This is a bounded next development cycle under the active wake goal.

Create incomplete-prefix/suffix negatives from TRAIN positive sources and
TRAIN board recordings; preserve source-group boundaries. Cut at50% of the
annotated time interval, jitter40..60% for a second training augmentation,
fade20ms. These are time cuts, not asserted phonetic word boundaries. Include
clean and TRAIN-response/noise versions. Hold out analogous VALIDATION cuts;
retain the original validation examples unchanged and do not extend test data.
Sampling25/25/20/10/20percent for zh/yue positives, existing hard negatives,
natural negatives and paired incomplete negatives; captured domain25percent.
Normalization and INT8 runtime remain unchanged. Freeze threshold on validation.

Before any C device deployment: report original/device language recalls,
standalone partial-word and derived paired-negative triggers separately. Target
zero partial and paired device triggers with no near-domain recall regression.
Retain failures; no automatic second C training run or test-threshold search.
The weak-level and real5m requirements remain unchanged and unverified.

## Third bounded cycle: sparse negative-event loss

C failed the frozen validation gate (paired device4/32, original device1/40
false triggers), despite zh8/8,yue7/8 valid. It was not flashed. Validation-only
diagnosis shows3 highest negative frames have2.27..3.95 loss while128-frame
mean is0.12..0.34 on four failing examples. High threshold950 still triggers
one paired example and loses five Yue positives. This suggests a loss dilution
problem, not proof of its cause; preserve all C scores and thresholds.

Run one D experiment: same paired dataset and architecture, initialize C best,
3000steps/batch32/lr0.00005/seed20260926. Add0.15times the mean softplus of
each entirely negative clip's top3 frames. Exclude positive and uncertainty
clips from this term. Use the same weighted objective for best-checkpoint
selection. No dataset/test changes or new threshold-search policy; same explicit
deployment gates. Unit test sparse gradients before training. Do not deploy a
candidate that fails these gates merely because the aggregate loss improved.

## Additional TRAIN-only measured examples

D reduces captured derived-partial triggers4/32to2/32 but captured Yue recall
falls7/8to5/8. Reject deployment. Fixed186example TRAIN float diagnostic shows
C and D both accept all22sampled captured positives and reject all24sampled
captured derived partials. This suggests source generalization limits; does
not justify a wider network or more iterations on the identical data yet.

Collect48additional recordings:24previously unused TRAIN sources,16positives
(8/language across available source groups),8standalonepartials(你好/小言 in
both languages), each at baseline0.35 and relative-12dB. All ancestry remains
TRAIN; no test/validation source reused for training. Freeze source selection
before playback, wait ADCready, capture6s and preserve raw received PCM/levels.
Use current B with wakeoff and unchanged microphone path; record firmware
identity. Preserve old clip before replacement; no new flashing. No automatic
training follows collection. Recheck alignment/clipping/received signal before
assembling a new development dataset. Background is uncontrolled and must be
reported; these captures are neither5m nor human generalization evidence.

## Measured-data candidate E after capture quality audit

All48recordings complete. Bounded waveform GCC-PHAT provides a TRAIN-only
delay fallback when the RMS envelope is masked by room noise. Require peak
RMS ratio>=7.5 and competitor ratio>=1.2 inside expected-40..+160ms. Tests
verify known filtered/noisy delay and reject12unrelated random sources. This
is alignment, not source identity:3/48same-phrase/different-voice controls also
match (all partial phrases); do not claim an identity or recognition proof.
Automatic local ASR of8fixed recordings finds speech at both levels, but
homophones and close spellings preclude a human-pronunciation claim.

Audit accepts41/48: zh19/24, Yue22/24; excludes all records with ADC clipping
or unreliable alignment. Low-level accepted zh12/12,Yue10/12 includes negative
clips, not wake-recall counts. Append TRAIN data only, normal and0.85/1.15speed
variants with scaled boundary intervals; reject variants exceeding the whole
word receptive-field budget. Keep original normalization, validation and test
byte-identical. No validation waveform fallback or labels changed.

One E run:4000steps,batch32,lr0.0001,seed20260925,initial Bcheckpoint,paired
negative sampling,devicefraction0.25,peak-lossweight0. Same settings as C to
test measured data coverage without conflating D's loss change. Fixed validation
gates stay in force; no automatic flashing or test-based threshold adjustment.

## Prefix reconstruction and joint initialization F

Reconstructed E's two captured prefix failures, reproduced normalized frontend
features exactly, and inspected local ASR of full/retained/removed portions.
They are not complete four-syllable words. Events occur788ms/513.5ms after
the cutoff. Do not remove these valid negative examples from the gate.

A finite88captured-validation pilot used recent PCM energy (3dB above initial
2.048s median,192ms hangover). It removes both false positives but lowers
zh8/8to6/8 and Yue7/8to4/8. Reject this prototype; it is not in firmware and
does not replace semantic full-phrase verification. Frozen score run lengths
also overlap between true and false events, so merely extending vote duration
is not justified as a solution.

Next one F run tests fresh joint-domain representation instead of successive
synthetic-initialized fine-tuning: same features-measured-v2 and24channel C11
network, random seed20260928,6000steps,batch32,lr0.001,devicefraction0.25,
paired-negative sampling and no peak/activity heuristic. Best checkpoint and
threshold use unchanged validation rules; all prior deployment gates stay.
No automatic follow-on run or device install is authorized by a lower loss.

## Fixed two-member fusion prototype

F standalone validation: zh7/8,Yue7/8,originalnegative0/40,pairednegative1/32;
not accepted alone. Fixed equal E/F logit averaging (no coefficient search)
removes captured negative triggers but retains two positive failures, including
one only1ms before the frozen lower endpoint. A second fixed recipe uses a
causal three32ms-block mean before the existing2-of-3vote, trading bounded
delay for less score jitter. Threshold750/Q8281 selected by the unchanged
validation ranking; captured zh8/8,Yue8/8,all72captured negatives0, including
32paired partials; standalone partial and overall-domain gates pass. Endpoints
and prior failed scores remain unchanged. Not a5m or independent test result.

Implement only this frozen recipe for a C/device experiment. Shared frontend
and two independent neural histories; observed host workspace12632B versus
single9448B. The first member trace remains separately inspectable, USB additionally
returns both logits, smoothed score and event. No inference-time allocation.
Host proofs: each member4096feature/512PCM frames exact,512fusionPCMframes and
256/512sample call paths exact; ASan/UBSan and no-allocation tests pass. RealC3
timings/heap remain to verify. Guard app-only flash with backup/non-app byte
comparison. Preserve generated/trained.c(B) and rollback; Fusion is opt-in.

## Bounded frontend compatibility pilot after physical fusion failure

Physical fusion near recall20/20each, but two小燕negative triggers; at-12dB
Yue1/4. Fixed diagnostic digital gain1/2/4 did not consistently solve it.
TRAIN-only median quiet spectrum from44original captures has86.6%power below
500Hz (including45.0%below125Hz). This is not proof of electrical noise source.

Before retraining, test one conservative causal per-band noise subtraction:
convert exact C Q8log-energy to approximate magnitude, track magnitude with
EMA1/32, subtract the estimate but retain at least50%current magnitude. Convert
back to log-energy and original normalization. This is a floating prototype,
not claimed bit-exact C or equivalent to PCEN. The rolling per-band subtraction
principle is documented in TensorFlow microfrontend noise_reduction.c; our
fixed conservative constants differ and no upstream runtime is introduced.
Reference: https://github.com/tensorflow/tflite-micro/blob/main/tensorflow/lite/experimental/microfrontend/lib/noise_reduction.c
PCEN is a possible jointly trained alternative, not a drop-in claim:
https://arxiv.org/abs/1607.05666

Reuse88captured VALIDATION examples reconstructed exactly, checking original
features and frozen E/F scores before the experiment. Keep original endpoints,
750threshold, fusion/vote rules. Evaluate original and two fixed attenuated
mixtures (-6/-12dB speech-plus-noise component plus independent shifted ambient
at complementary RMS weight). These are approximate offline perturbations,
not physical distances, fresh captures or independent examples. Noise segments
come from each example's own validation capture; no parameter fitting on TEST.
Maximum264waveforms, baseline and one frontend recipe, no grid/extra training.

Advance only if all original captured positives and negative gates remain
intact and attenuated positive misses decrease without extra negative clips.
Otherwise reject drop-in deployment and retain the failure. Any later training
with a changed frontend needs explicit numerical/data/resource gates, and
must exclude the ten unblinded TEST diagnostic recordings. No more flashing
or repeating the ADC attenuation A/B/A is part of this pilot.

## Fixed-point PCEN prototype, separate from active recognition

Drop-in subtraction pilot failed: original zh/Yue8/8to7/8 with2new negatives;
perturbed Yue4/8to2/8 at-6dB and2/8to1/8 at-12dB. Do not deploy that recipe.
Begin a separately trained frontend route only after numerical/resource proof.

Prototype PCEN parameters are fixed, no search: magnitude=floor(sqrt(Melpower));
Q16 EMA smoothing1/32; alpha1,epsilon1magnitude unit,delta2,r1/2. Ratio uses
Q12 integer division and outputQ8 integer square root. State160B/40bands;
no float, heap allocation, generic inference library or runtime model change.
Its weights/normalization must be trained for this representation; old log-Mel
weights must not be silently reused. PCEN research is motivation, not C3 proof.

Host acceptance:4096full-range frames exactly match an independent Python
math.isqrt oracle; error under1.01Q8unit against the floating formula on the
same rounded magnitude/EMA; silence/reset/extremes under ASan/UBSan. All passed.
Add only a reserved-session USB diagnostic `agent kws pcen-check`,256fixed
adversarial frames, returning CRC/state size/max/total time/heap. This function
does not touch the live recognizer state. Require matching host CRC,160Bstate,
per16ms-frame incremental time<1500us and bounded command completion<500ms.
Measure on C3 before expanding data/starting a PCEN training run. Guarded
application-only update preserves current E/F weights,750threshold and data.
No changed audio frontend is enabled in the firmware by this prototype.

## One paired PCEN training candidate G

After standalone C3 proof (285us maximum,160B state), expose the exact existing
FFT/Mel power to an explicit PCEN frontend function. Keep default log-Mel path
unchanged and require512PCM-frame equality against the old library plus PCEN
integer-oracle equality before generating features.

One corpus, maximum16000examples, existing TRAIN/VALIDATION sources only; no
TEST WAVs, no ten diagnostic captures. Existing measured response and44ambient
TRAIN captures supply channel/noise augmentation. Three synthetic TRAIN variants
(clean plus two measured-channel/noise variants,-6..18dB approximate SNR), full
phrases and their retained-half negatives. Original72captures and41audited TRAIN
supplemental captures provide device examples. Captured TRAIN augmentation uses
0/-6/-12/-18dB mixtures; VALIDATION uses original plus separately reported
-6/-12dB mixtures. All source ancestry is retained, normalization fits TRAIN
PCEN features only. Data/front-end changes are joint, not a pure ablation claim.

G budget: one6000step,batch32,seed20260930,lr0.001,fresh initialization,
24channels, paired-negative sampling,devicefraction0.25,no peak-loss heuristic.
Best checkpoint and fixed threshold use original validation only. Calibration
uses TRAIN only. Do not select thresholds on weak/test replays.

Advance to device experiment only if original captured8/8each and0/72negatives,
overall validation>=90%each,negative<=5%,no standalone partial triggers; weak
validation cannot regress each language against frozen E/F and must gain at
least two combined Yue hits across-6/-12dB. Retain a failed run without an
automatic second training. Active model remains E/F until these gates and
trained C/board/resource proofs pass. Full5m acceptance remains separate.

### G failure diagnosis (no second training or device activation)

G failed its frozen gates. Complete one read-only diagnosis: all unattenuated
captured TRAIN examples (variant0, at most300), original/weak saved validation
scores, fixed probability500..950 by10 descriptive threshold envelope, float
versus INT8 score differences, input saturation and per-clip failure category.
Record selection before scoring and check checkpoint/features/threshold hashes
before and after. No TEST access, training, threshold replacement or flash.
The envelope determines whether threshold-only changes could satisfy the
original captured gates; it does not authorize tuning to weak validation.

## Bounded capacity diagnosis after G (TRAIN only)

G float still has5/219negative events on its268unattenuated captured TRAIN
subset. Before increasing device RAM/compute or starting another full candidate,
check whether the24channel network can fit that fixed small set. Seed20261001,
1500steps,batch32,lr0.001,ordinary masked BCE,25/25/50percent zh/Yue/negative
sampling,4CPUthreads,fresh initialization. Evaluate final step only at G's
frozen float threshold295; no validation/TEST file access or threshold search.
If full recall and zero false/premature events pass, skip wider model. Otherwise
run exactly one48channel host-only comparison with identical inputs/seed/budget.
No further widths/seeds/retries, no calibration/export/flash. Save all failures.
Both checkpoints are explicitly diagnostic-only, not release candidates.

48channel arithmetic:5*C*C+158*C+1=19105training parameters versus6673at24;
INT8convolutionweights18528B versus6384B. Existing C runtime accepts24only;
no48weights may be installed without new C parity and actual C3 resource gates.
Fitting a small TRAIN set proves neither human generalization nor5m operation;
failure after1500steps is not proof that the architecture cannot fit it.

## Candidate H: one measured-domain adaptation, same frontend and capacity

Capacity probe24fits49/49positives and0/219negatives/premature events after
1500steps.48skipped by rule; no evidence to spend device resources on widening.
This is not held-out quality. Register one2000step H adaptation from Gbest,
not from the small-set diagnostic checkpoint. Same frozen PCEN features,
normalization,24channel architecture and paired sampling; only train-device
fraction0.25to0.75 (and the corresponding existing validation domain weights),
learningrate0.0001,seed20261002,batch32,4threads. No new data or noise recipe.
Calibration TRAIN-only. Freeze threshold on original validation; evaluate
weak6/12once afterwards with G's unchanged gates. Stop on gate failure without
automatic second adaptation, threshold retuning or activation. No TEST access.
Source/data/checkpoint hashes frozen before and verified after the run.
If gates pass, require active-PCEN C/board numeric and memory/timing proof
before physical replay; still no claim of real5m or human-speaker success.

## Measured near-word contrast coverage,32captures only

H also fails original captured gates and weak false-event gates. Close the
current PCEN candidate branch; no third adaptation. Coverage inspection finds
7syntheticTRAIN小燕negatives per language, but zero actualTRAIN小燕captures;
the existing measured near-word bucket contains小王. Add one fixed32capture
contrast set: TRAIN voicesxiaoni,xiaoyi,yunxi,yunyang, both languages, one full
positive and one你好小燕negative per voice/language, normal0.35 and -12dB.
Source choice is stable clip-ID hash ordering, before scoring.16distinct sources,
same ancestry/split retained, no TEST recordings reused. Same current E/F app
and recording path; output resolves namedMisiom-Shooter, not a changing index.
Original recording slot exported first; capture windows6s. Failures retained,
no automatic rerun. Audit ADC clipping, alignment and background before any
feature preparation. This adds TRAIN coverage, not new blind evaluation,
pronunciation verification or physical5m evidence. No automatic training.
Score eligible captures once with the frozen E/F host library/hash and750/Q8281,
using the earlier explicit2.048s own-ambient prefill and unchanged event rule.
Retain all events, positive endpoint-window validity, premature events and
negative triggers. This is a pre-adaptation TRAIN baseline; no threshold or
gain fitting, no claim to reproduce the original live listening history.

## Candidate I: one log-Mel contrast adaptation with fixed E/I fusion

Return to the better-performing log-Mel route; PCEN G/H remain rejected.
Append28audited TRAINcontrast recordings to features-measured-v2 using the
existing bounded speed/gain/partial/ambient recipe, seed20261003. Keep original
validation/test/normalization byte-identical and old training arrays as exact
prefix. Retain source/recording ancestry, reject clipped/unaligned inputs.

One2000step adaptation of Fbest,lr0.0001,batch32,4CPUthreads,seed20261003,
devicefraction0.25. Same network and ordinary BCE, no peak-loss change.
Split the existing20%hard-negative batch share into10%小燕and10%other hard
negatives; preserve25%zh/25%Yuepositives,10%natural,20%partial negatives.
This is a joint measured-coverage/sampling experiment, not a causal ablation.

Keep member E fixed; candidate pairing is E/I only, equal logits plus the
already implemented causal3blockmean. No ensemble weight/window/member search.
Threshold uses original validation only. Gates: captured8/8each,0/72negative,
overall>=90%each,negative<=5%,standalonepartials0 and synthetic小燕validation0.
Then once-only fixed weak6/12validation mixtures at frozen threshold: each
language nonregression versus E/F,zero added negatives,>=2combined extra Yue
hits. No TEST scoring. Failure stops this candidate, no automatic second fit.
Passing host gates permits C/board numerical/resource checks and physical
replay, not a claim of5m. Network/runtime budgets stay equal to existing E/F.

### I failure source diagnosis

E/I passes original captured8/8each and0/72negative but still triggers one
synthetic Yue小燕validation example. Stop deployment, preserve frozen830threshold.
One source-pitch check: the failing validation source, six same-voice positives
selected by clip-IDhash, and seven TRAIN小燕sources with one matching-voice
positive each (21total). No TEST. Local installedlibrosa0.10.2pYIN,65..500Hz,
1024sampleframes/160hop,centerFalse; retainvoicedprobability>=0.8. Check120/240Hz
controltones first, then report final350ms contours against automatic endpoints.
Reference https://librosa.org/doc/0.10.2/generated/librosa.pyin.html.
F0 is not a phonetic classifier; do not relabel/exclude a difficult validation
example based on pitch or ASR text, and do not train or retune from this probe.
