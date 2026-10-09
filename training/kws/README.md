# Local wake-word pipeline

The Phase 1 probe is a computation/resource prototype, **not a trained wake-word
model**; it cannot wake the device. Phase 2 below adds the actual local training
and learned-model export workflow. A completed training run is not proof of a
passed acoustic quality gate. No recordings leave the computer.

Environment tested: WSL Ubuntu, Python 3.12, GCC 13.3, CMake, Ninja; PyTorch
2.6.0+cpu. A GPU is unnecessary for these small deterministic checks.

The trainer accepts `--channels 48`; default24 and legacy24 checkpoints remain
supported. Width is validated against the stem tensor before calibration. A48
export requires a48 compiled C ABI and emits a compile-time guard. This is an
experimental training/export option, not a passed replacement wake model. The
first complete48 candidate failed acoustic admission and was not flashed; see
`docs/WAKE_WIDE_TRAIN_REPORT.md`. Existing QAT experiments retain their original
contracts; they have not all been widened. The device's current model is unchanged.

```sh
sudo apt-get install python3.12-venv
python3 -m venv ~/.venvs/cogd-kws
~/.venvs/cogd-kws/bin/pip install torch==2.6.0 --index-url https://download.pytorch.org/whl/cpu
~/.venvs/cogd-kws/bin/pip install -r training/kws/requirements.txt
~/.venvs/cogd-kws/bin/python training/kws/export_c.py
cmake -S tests/kws -B build-kws-host -G Ninja
cmake --build build-kws-host
ctest --test-dir build-kws-host --output-on-failure
~/.venvs/cogd-kws/bin/python -m pytest -q tests/kws/test_python.py
~/.venvs/cogd-kws/bin/python tools/kws/parity.py
~/.venvs/cogd-kws/bin/python tools/kws/verify_sources.py
```

`requirements-cpu.lock.txt` also pins the resolved dependencies of this tested
environment. Reinstall it with the same PyTorch CPU wheel index when reproducing
the complete environment rather than only the direct dependencies above.

Run from the project root. Windows device commands use the existing private
ESP-IDF Python. `tools/build_kws.ps1` selects a new build directory and disables
the problematic ccache. It does not flash anything.

```powershell
./tools/build_kws.ps1 -Mode probe
./.toolchains/tools/python_env/idf6.1_py3.11_env/Scripts/python.exe tools/kws/flash_guard.py
./.toolchains/tools/python_env/idf6.1_py3.11_env/Scripts/python.exe tools/kws/serial_test.py parity
./.toolchains/tools/python_env/idf6.1_py3.11_env/Scripts/python.exe tools/kws/serial_test.py benchmark
```

`flash_guard.py` saves a new full Flash backup, verifies it against the stopped
device, checks the unchanged partition table and application size, writes only
the application, then reads all Flash again and compares every non-application
byte before starting the new firmware. Close the serial terminal first.
`firmware/rollback/0.6.3-context/rollback.py` restores the verified previous
application without changing NVS, context or recording partitions.

Probe USB commands:
- `agent kws begin`: reserve a temporary diagnostics session; wake must be off.
- `agent kws frame SEQUENCE CRC32_HEX PCM16_LE_HEX`: 256 samples (512 bytes),
  beginning at sequence zero. CRC/sequence errors do not advance inference.
  Response contains all 40 log-Mel values, 40 input INT8 values, 265 layer INT16
  values encoded in that order as little-endian hex, plus compute microseconds.
  An explicit48 build returns529 layer values; use `serial_test.py parity --channels 48`
  and a matching reference. Reliable replies split writes at the existing2048B
  atomic TX capacity without enlarging the ring or heap.
- `agent kws end` or `agent cancel`: release the session. 30 seconds idle also
  releases it. At most 4096 frames per session. Conflicting operations are busy.
- `agent kws reset-profile`: reset counters with listening off.
- `agent kws profile`: real-microphone 512-sample processing count, maximum and
  500 us histogram bins. Last bin includes all values >=31500 us. Timing includes
  the frontend, two network steps and detector, and any preemption inside that
  interval; USB transfer waiting is outside it.

The same C frontend is the intended feature producer for later local training;
the ctypes adapter in `tools/kws/parity.py` demonstrates its use. `model.py`
contains the matching causal float architecture and streaming reference;
`quantize.py` provides the independent integer oracle and BN folding helper.
`export_c.py` currently exports reproducible **probe** weights only; do not use
it as a purported trained-checkpoint exporter.

Callers must allocate `kws_workspace_size()` bytes aligned to
`kws_workspace_alignment()`. The ESP32-C3 adapter explicitly uses IDF's aligned
allocator: ordinary IDF malloc guarantees only four-byte alignment, whereas the
state's 64-bit sample clock requires eight. The USB session allocation also uses
its declared alignment. A misaligned workspace is rejected before any access.

Design and gates: `docs/WAKE_XIAOYAN_PHASE1_SPEC.md`. Frozen resource arithmetic
and hashes: `components/kws_c11/generated/manifest.json`. Source reference:
Google Research DS-TC-ResNet, pinned in `third_party/kws_ref/sources.json`.
That reference is not imported or linked at runtime.

## Phase 2 trained checkpoint workflow

The real trainer and exporter are separate from the reproducible untrained
probe. See `docs/WAKE_XIAOYAN_PHASE2_SPEC.md` for finite experiment limits.
Do not copy probe weights into a trained build or report synthetic test results
as human speaker generalization.

Screen source manifests with `training/tts/curate.py` first. It rejects uncertain
pronunciations, mismatched languages and incomplete/overlong words; carrier
sentences require approximate timestamp/energy alignment and re-recognition
of the isolated word. Original sources and all rejection reasons remain local.

Example commands below run from the repository root inside the existing WSL
KWS environment. Output directories are new, immutable experiment locations.

```sh
python training/kws/assemble.py \
  artifacts/kws-phase2/curated-zh-bounded1/manifest.jsonl \
  artifacts/kws-phase2/curated-yue-bounded1/manifest.jsonl \
  artifacts/kws-phase2/fleurs-v2/manifest.jsonl \
  artifacts/kws-phase2/backgrounds/manifest.jsonl \
  --out artifacts/kws-phase2/reproduction/dataset
python -m training.kws.cli prepare \
  --manifest artifacts/kws-phase2/reproduction/dataset/manifest.jsonl \
  --out artifacts/kws-phase2/reproduction/features --augmentations 3
python -m training.kws.cli train --features artifacts/kws-phase2/reproduction/features \
  --out artifacts/kws-phase2/reproduction/baseline --steps 1000
python -m training.kws.cli calibrate --features artifacts/kws-phase2/reproduction/features \
  --checkpoint artifacts/kws-phase2/reproduction/baseline/best.pt \
  --out artifacts/kws-phase2/reproduction/baseline-int8
python -m training.kws.cli evaluate --features artifacts/kws-phase2/reproduction/features \
  --checkpoint artifacts/kws-phase2/reproduction/baseline/best.pt \
  --bundle artifacts/kws-phase2/reproduction/baseline-int8 --split validation
```

Only after a validation diagnosis and model/threshold freeze may the same
evaluation command run with `--split test`. The dataset assembly gate requires
100 unique positive PCM files per language in the reserved test set; it does
not equate these files with 100 voices. Any subsequent training informed by a
test result must declare that set development data and use a fresh held-out
source group. At most baseline plus two diagnosed training adjustments.

After that evaluation, run `python training/kws/quality_gate.py --bundle
artifacts/kws-phase2/reproduction/baseline-int8 --features artifacts/kws-phase2/reproduction/features
--dataset artifacts/kws-phase2/reproduction/dataset`. This binds input hashes, exported C,
both-language recall and quantization loss. It proves only the host quality
gate; actual device acoustic/resource evidence is still required.

Normalization and calibration use training sources only. Augmented training
examples include gain, low Gaussian noise, limited early reflections and
optional background audio from training FLEURS/procedural sources. The feature
manifest records mixed source IDs. Validation and test files are unchanged.

After quality gates pass, copy the actual exported `model.c` to
`components/kws_c11/generated/trained.c` and retain its JSON/config/hash. Build
the host library using `-DKWS_MODEL_SOURCE=<absolute path to model.c>` and run
`tools/kws/parity.py --model <bundle/model.json> --library <learned libkws.so>
--symbol kws_trained_model --out <new parity directory>`. Then build firmware
with `tools/build_kws.ps1 -Mode trained`. A missing trained source is an error,
never a silent fallback. The trained firmware identifies itself as
`0.7.0-xiaoyan-exp`; legacy/probe build identities remain distinct.

Use `tools/kws/flash_guard.py --phase phase2 --build build-kws-trained` only
after the application, data-layout and quality checks. It makes a fresh verified
backup before writing only the application. Run board parity with the same
learned traces. `tools/kws/acoustic_test.py replay|observe --manifest <frozen
manifest> --threshold <bundle/threshold.json> --out <new directory>` refuses
legacy and untrained firmware. Replay is capped at 20 reserved clips per
language; observation at 30 minutes. Observation records ambient and controlled
interference separately, actual inferred audio duration and all triggers.
Uncontrolled room sound is not silently classified as verified silence.

## Experiment evidence and replay constraints

The original `fleurs/` conversion incorrectly read normalized FLOAT WAV as
integer PCM without scaling. `dataset-v1` rejected duplicate PCM hashes before
training. Use the corrected `fleurs-v2/`; the failed data remain diagnostic
evidence, not a training source. The first valid experiment used `dataset-v2`
and `features-v2`. Do not overwrite completed experiment directories.

After adjustment1 failed independent Cantonese recall, `retire_test.py` moved
the old synthetic test identities into training and public test negatives into
development validation. The changed IDs/hashes are recorded in
`development-final/retirement.json`. Those results are no longer blind evidence
for adjustment2. Its reserved voices are af_heart and am_michael; their source
descendants stay exclusively in the final test split. Distinct synthetic preset
identities do not prove distinct human speakers or independent TTS engines.

The registered final run uses `dataset-final`, `features-final`, 2000 steps and
`--boundary-negative-weight 3`, with output `adjustment2`/`adjustment2-int8`.
It is the last of three allowed training runs. Freeze the validation-selected
threshold and bundle hashes before evaluating its new test set once. Failure
does not authorize additional training, data generation or relaxed criteria.

For a passed final candidate, run host parity using that exact learned library
and model, then board parity using the generated PCM/traces. Reports bind the
model/library/trace hashes; host inference timings are not C3 performance.
The trained Agent regression command is `tools/test_device.py --version
0.7.0-xiaoyan-exp --wake-model xiaoyan_ds_tcn24_v1 --trained-kws --cloud
--output <new directory>`. Restore the validated threshold and listening state
after diagnostics; cancellation and the regression script can stop listening.

The final registered run failed held-out Yue recall (107/147, 72.79%; Mandarin
241/243). Do not deploy it as an accepted model or rerun training automatically.
`diagnose.py --bundle <bundle> --features <features> --manifest <manifest>
--out <new json>` explains already-frozen test scores and event offsets; it does
not choose a new threshold or change the result. See the Phase 2 report for the
finite stopping decision and remaining device checks.

## Phase 3: corrected endpoint intervals, one completed run

Phase 2's failure remains immutable. The separately authorized Phase 3 run uses
`boundary.py`/`relabel.py` to separate retained source audio from the target-end
interval. `data.py` preserves onset/tail context and matches placement of short
positive/negative clips. Unknown-boundary labels are -1 and `masked_bce` omits
their gradients. Evaluation uses the frozen lower endpoint through upper+800ms;
this new contract must not be used to rewrite the old published results.

The completed run is in `artifacts/kws-phase3`: 6000 steps, batch32, seed20260922,
`--boundary-negative-weight 1 --balanced-hard-negatives`, one checkpoint search
using validation loss only. New held-out voices af_bella/am_fenrir replace the
failed bf_emma/bm_george pilot; old final test voices are development sources.
The initial/fallback pilots, rejection records and source hashes are retained.

Host test passed: Mandarin232/234, Cantonese149/155, negative3/85, partial0/7,
quantization loss0/0.645percentage-points. The exact C export passed two integer
oracles and fixed-PCM checks. No device was flashed; synthetic data and automatic
boundaries do not establish real-speaker accuracy or continuous false alarms.

Use the frozen `trained/training-source/` snapshot and configs when reproducing
training, with fresh output directories. `tools/kws/finalize_phase3.py` is the
one-shot Windows/WSL orchestrator: it verifies the fixed training budget, freezes
the candidate, evaluates once, checks `quality_gate.py --partial-word-gate`, and
builds/checks a host library using the actual export. It refuses an existing
selection record. Do not rerun it over the completed evidence directory.

Full results and limitations: `docs/WAKE_XIAOYAN_PHASE3_REPORT.md`.

## Phase4 physical outcome
See docs/WAKE_XIAOYAN_PHASE4_REPORT.md. The frozen Phase3 model failed physical replay (Mandarin0/20,Cantonese1/20) despite exact board parity. Device restored to0.6.3; generated/trained.c is retained for reproduction, not an accepted replacement. Training and threshold remain frozen.

## Phase5 active device-domain development

Phase4's rollback is historical. Phase5 currently has the experimental E/F
fusion installed (`0.7.0-xiaoyan-exp`, model `xiaoyan_ds_tcn24_ef3`, threshold750).
It shares one frontend, retains two independent neural histories and averages
three successive block logits. Workspace12632B; board parity512frames exact.
Physical near regression:20/20valid per language,partial0/7,but negatives2/20
(both你好小燕). At-6dB4/4per language; at-12dBzh4/4,Yue1/4. Thus acoustic
acceptance fails and5m remains unverified. Do not deploy altered thresholds
from these now-unblinded cases. Ten separately marked TEST diagnostic recordings
are excluded from training; simple digital gain does not consistently fix them.
The opt-out `generated/trained.c` remains candidate B (near-field18/20valid per
language, negatives5/20). Separate fusion sources hold E/F. C/D/E/F standalone
candidates failed their captured partial-phrase gates. Legacy rollback remains.

See `docs/WAKE_XIAOYAN_PHASE5_SPEC.md` and `docs/WAKE_XIAOYAN_PHASE5_REPORT.md`
for immutable per-candidate evidence and the still-unverified actual5m goal.
Do not rerun one-shot orchestrators over existing artifact directories. Each
candidate stores a training-source snapshot, configuration, seed, feature/model
hashes and frozen validation threshold. A lower development loss is not release
acceptance. Captured synthetic replay does not prove human-speaker generalization.

`append_captures.py` uses only source-separated, audited TRAIN recordings and
keeps original validation/test bytes. `activity_gate_pilot.py` is a rejected
offline prototype; it is intentionally not in firmware because it loses weak
positive speech. `train_joint_candidate.py` runs the finite F joint-domain
initialization experiment without automatic flashing or test scoring.

`noise_frontend_pilot.py` is a rejected single-recipe compatibility experiment;
do not put it in the active inference path. `kws_pcen.c` is a different,
standalone fixed-point PCEN prototype for a future jointly trained frontend.
Its160Bstate and arithmetic have host checks plus C3 CRC/timing evidence under
`artifacts/kws-phase5/pcen-prototype`. The current app exposes only the bounded
`agent kws pcen-check` diagnostic inside an existing `agent kws begin` session.
It does not apply PCEN to E/F. New weights, exact frontend data and normalization
are required before activation. Candidate G has now completed one6000step
joint PCEN/data run and failed deployment gates; it is not installed.

`prepare_pcen.py` uses the exact C frontend, TRAIN-only normalization and
source-separated development inputs. Its11191TRAIN/714validation examples
exclude TEST WAVs and the ten diagnostic captures. `train_pcen_candidate.py`
freezes one run; `evaluate_pcen_weak.py` reports fixed-threshold weak mixtures.
Do not rerun these one-shot scripts over completed output directories.
`diagnose_pcen_candidate.py` preserves a268-example captured TRAIN selection
and reads saved validation scores, without retraining or changing thresholds.
G captured original zh7/8,Yue7/8,negative5/72; approximate-6/-12dB Yue4/8,3/8
but negatives6/72,12/72. Float also fails; no threshold in the registered
500..950permille grid satisfies full captured recall with zero false events.
Keep G as failed evidence. Current E/F device behavior and5m limitations above
are unchanged. See `artifacts/kws-phase5/pcen-g-int8/weak-validation.json` and
`diagnosis.json`; these development results do not establish human generalization.

`capacity_probe.py` then fitted the frozen268-example TRAIN subset in1500steps
with the original24channels (49/49positive,0/219negative,premature0), so its
conditional48channel comparison was skipped. The diagnostic checkpoint is
not deployable. Optional48channels exist only in the host model for this
bounded comparison; C inference/export and ordinary training still use24.

One2000step H adaptation from Gbest increased measured-domain sampling to0.75.
It also failed unchanged gates (original captured7/8each,4/72negative;weak12
Yue5/8but13/72negative). No PCEN recognition firmware is installed. This closes
the current PCEN candidate branch; no automatic further adaptation.

`capture_word_contrast.py` registers32TRAIN-only physical captures to fill the
zero-recorded-小燕 coverage gap: four voices, two languages, fullword/小燕,
normal/-12dB. It never trains. `audit_capture_supplement.py --expected-trials 32`
checks source hashes, alignment and
clipping before use. Existing48capture audit remains the default. This is
development data, not an independent voice or physical-distance acceptance set.

Candidate I appends190contrast examples to14170old TRAIN examples, preserving
old arrays and validation/test/normalization bytes. Fixed2000step adaptation
of F and fixed E/I fusion pass old captured8/8each,0/72negative, but still
trigger a synthetic Yue小燕source. Frozen830threshold weak6zh6/Yue2andweak12
zh4/Yue0(outof8each),zero negatives. Reject deployment; no repeated fit or
threshold adjustment. `train_contrast_candidate.py` and
`evaluate_contrast_weak.py` retain this failure. Device is still E/F.

`audit_yue_pitch.py` preserves21source contours and checks controltones; it
cannot establish phonetic truth and must not be used to relabel hard examples.
Further field acceptance needs independent human recordings/real distance.
`capture_human_review.py` is a local user-operated entry point, not a background
recording/training job; its output defaults to independent review, not TRAIN.
# 训练目标的新增检查（UX266–268）

UX269–271新增`negative_groups.py`保留全部负例并显式平衡近词/片段/自然/其他；一次完整48模型误触总量显著减少，但新增事件门槛失败。固定共同分数规则也因漏醒拒绝，未默认采用；见`docs/WAKE_BALANCED_TRAIN_REPORT.md`。

UX272的`event_confirmation.py`只配对已接受事件并复用原统计定义。
默认固件未增加运行资源；C模块/ASAN边界与数值事件测试通过，但召回失败，
没有采用该规则。见`docs/WAKE_EVENT_CONFIRMATION_REPORT.md`。

UX273–276的`anchor_loss.py`只给已标注正例的原合法时刻增加监督，
保留禁止/提前事件损失；5个独立目标测试通过。`wake_owner.py`和实验C模块
只允许在原接受端点核验，没有辅助接受事件冷却或额外等待。
一次完整训练及固定复合通过TRAIN准入，已知DEV召回失败，尚未默认采用。
见`docs/WAKE_ANCHOR_OWNER_REPORT.md`；TRAIN改善不能作为真人泛化结论。

UX277的`wake_temporal_owner.py`是固定256ms、一次原事件拥有权的因果oracle。
召回/误触通过已知对照，但粤语P95新增延迟104ms超过96目标，未默认采用。
UX278仅诊断C内核注册宽度的显式混用，默认24/48数值/ABI/初态保持；没有C3预算或现场验收结论。
见`docs/WAKE_TEMPORAL_OWNER_REPORT.md`。

UX279新增`kws_verified`显式运行时，共用FFT、保留24E/24L/48三份NN历史；
typed初态由`tools/kws/generate_verified_silence.py`和`export_verified_silence.c`生成，
模型输入必须显式提供，包含其输出CMake hash检查后才能启用恢复。
62206帧主机分数/事件及所有因果初态、无分配/取消/间断检查通过。
主机workspace24328B、C3实际24320B、typed常量12992B；默认关闭。
UX280–281仅资源诊断，保留277延迟失败，不能当真人泛化或业务通过。
见`docs/WAKE_VERIFIED_RUNTIME_REPORT.md`。

`event_loss.py` 是只在主机运行的实验损失，精确前向对应Q8/三块均值/twoof3，区分允许与禁止端点并排除无效尾。
`test_event_loss.py` 核对4096分数/C事件、因果前缀、梯度和预热/尾mask。
一次完整48训练清除了已知设备采集负例，但完整旧对照回退及新增自然粤语误触使候选被拒绝，未烧录。
临时trainBN仅用于非因果诊断，不能作为设备推理模式。见`docs/WAKE_EVENT_OBJECTIVE_REPORT.md`。


`negative_prefix_loss.py`是UX306新增的主机实验项，补齐已标注负类预热前真实帧的机会hinge和BCE，排除正父源与无效尾，不修改C端预热或判定。可运行`python training/kws/test_negative_prefix_loss.py`复现语义/梯度检查。一次固定训练与整数准入见本地`artifacts/voice-fast/wake-prefix-fit-ux306/`及`wake-prefix-admission-ux307/`：旧负窗口20>18而拒绝，不是默认训练项或设备模型更新，禁止以此成绩宣称现场改善。


`revocation_anchor_loss.py` 是UX311的主机实验监督项：正锚点目标与当前负分撤销票数的固定原型一致，原负例和前缀损失保持。调用者使用原`accepted_anchor_mask`排除负类与无效尾。`test_revocation_anchor_loss.py --library <support.so>`需配套C原型支持状态fixture，实现与复现规格见`docs/WAKE_REVOCATION_OBJECTIVE_REPORT.md`及本地UX311证据。该项未加入默认训练器或固件，数值检查通过不代表新模型识别效果通过。


## Named-word auxiliary experiment (2026-10-04, UX494–496)

`recurrent_words.py` uses the8 audited complete source names as9 semantic event classes including blank; it does not invent phone/tone annotations. Unknown/incomplete speech has no named-word event. `train_recurrent.py` enables this host-only mode only with `named_word_regularizer=true`; existing event/phonetic modes remain. Host7467 parameters project exactly to the unchanged7170-parameter event2 C11 contract; no new firmware runtime is enabled.

Three independent path/gradient/source/projection checks and legacy checks passed. SignedstoredwordIDs widen toint64; the initial int8 preflight error is preserved with0 optimizersteps. The single6000step actual fit FAILED unchanged quality gates: nearerrors25/796 but natural103/2225,total321/9496,ZH81.99%,Yue78.44%. No PTQ/C export,DEVTEST,device audio or Flash. Do not restart the completed recipe or present its TRAIN scores as field/human quality. Evidence `artifacts/voice-fast/wake-named-word-repair-ux496/`; current verified device remains original72/wakeoff/voiceoff.
