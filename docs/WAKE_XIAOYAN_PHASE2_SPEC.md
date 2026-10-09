# Phase 2: bounded local bilingual wake-word training

Approved scope: Mandarin and Cantonese pronunciations of `你好，小言`, one
positive class; local reproducible training and an experimental device build.
Phase 1 is complete; its seed weights are NOT a trained wake model. The device
starts this phase on the verified 0.6.3-context / wn9s_hilexin rollback.

## Requirements and evidence

| ID | Requirement | Required evidence |
|---|---|---|
| XY20 | Verify local Mandarin/Cantonese synthesis and source licenses before scaling | pinned source/model revisions, license copies, pilot WAVs, pronunciation checks |
| XY21 | Split by source voice and recording ancestry before augmentation | dataset manifest, leakage tests; no pitch-shifted voice counted as new speaker |
| XY22 | Complete real training, calibration and C export | seed/config/environment, checkpoint, train-only frontend statistics and calibration, hashes |
| XY23 | Integer parity and quantization quality | independent integer oracle/C/device traces; <=5 percentage point recall loss per language |
| XY24 | Bounded experiments | baseline plus at most two diagnosed adjustments; no automatic unlimited training/search |
| XY25 | Experimental held-out target | >=100 distinct reserved synthesis/recording samples per language, >=80% recall each; repeated playback not independent samples |
| XY26 | Physical replay and background diagnostics | per-language misses and latency; 30-minute silence/interference observation, actual enabled duration and event counts |
| XY27 | Resource and Agent regression | unchanged partitions/context, app<=1540096 B, KWS RAM<=20 KiB, min internal heap>=32 KiB, largest listening block>=24 KiB, p99<=16 ms/max<32 ms per 512 samples; no frame loss/reset |
| XY28 | Safe experimental delivery | verified backup, application-only write, retained compilable 0.6.3 rollback, final installed identity and manifest |

Training and testing stay on this computer. Downloads are permitted; private
recordings are not uploaded. No paid/cloud training or TTS prerequisite.
CosyVoice2 Mandarin and Yue are the researched first candidate. Download and
environment preparation are not proof that generation or pronunciation works.
Do not clone an identifiable person's voice from unrelated recordings. Use
licensed model-provided synthetic voices or explicitly authorized references.

Test-set results are reported after thresholds are frozen using validation.
If a failed test informs another attempt, mark that test set as development and
reserve a fresh source group for the next test. Report synthetic direct-input,
speaker replay, and human live speech separately. Human generalization remains
unverified without independent human material. A short background observation
does not establish a production false-alarm rate.

Stop expanding data if the local generation pilot, training closure, numerical
parity or resource gates fail. Preserve failed evidence and rollback rather than
shipping a broken always-triggering or untrained model as a working wake word.

Artifacts (ignored by Git): `artifacts/kws-phase2/` and new timestamped backups.
Source/scripts/tests and this specification remain reviewable in Git.
