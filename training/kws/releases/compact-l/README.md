# E/L fixed comparison candidate

This directory preserves the exported L model's training provenance and fixed
comparison inputs. The firmware weights are in
`components/kws_c11/generated/fusion_secondary_l.c` and the complete quantized
model is in the adjacent `fusion_secondary_l.json`. `manifest.json` hashes both
files, the retained E/K sources and all saved experiment metadata. E/K remains
the default. No recording or checkpoint is required to compile either variant.

L is a single 2,000-step adaptation from K, seed 202609221. Validation loss chose
step 150. Only licensed local CosyVoice2 supplemental Mandarin sources were
used in training. Original training and validation voice groups remain separate;
normalization and the old TEST file remain unchanged. SAPI sources were used
for diagnosis only. Yaoyao outputs duplicated Kangkang exactly and are explicitly
excluded from the independent-source count.

Mandarin digital-input recall improved, Cantonese and captured-development
recall were retained, while natural negative clips triggered increased from
4/636 to 5/636. This is a measured tradeoff to test on the same device; it does
not establish human bilingual generalization or require automatic rejection.
See `docs/WAKE_BILINGUAL_REPAIR.md` for limitations and prior evidence.

Build an isolated L application without flashing:

```powershell
./tools/build_kws.ps1 -Mode trained -Fusion -FusionPair el -WakeThreshold 740
```

Use the existing guarded application-only installer after a fresh backup.
The existing E/K build and C sources remain available for rollback. E/F is also
still explicitly selectable. These choices do not change the partition table.

Check the 14 local sources without opening USB or any audio device:

```powershell
./.toolchains/tools/python_env/idf6.1_py3.11_env/Scripts/python.exe -X utf8 tools/kws/bilingual_replay.py --check
```

Run the same source list before and after the guarded application update:

```powershell
./.toolchains/tools/python_env/idf6.1_py3.11_env/Scripts/python.exe -X utf8 tools/kws/bilingual_replay.py --pair ek --out artifacts/kws-bilingual/replay-ek
./.toolchains/tools/python_env/idf6.1_py3.11_env/Scripts/python.exe -X utf8 tools/kws/bilingual_replay.py --pair el --out artifacts/kws-bilingual/replay-el
./.toolchains/tools/python_env/idf6.1_py3.11_env/Scripts/python.exe -X utf8 tools/kws/bilingual_replay.py --compare artifacts/kws-bilingual/replay-ek/replay.json artifacts/kws-bilingual/replay-el/replay.json --out artifacts/kws-bilingual/acoustic-comparison.json
```

The entry point temporarily disables the voice network flow, exports the old
recording slot, fixes threshold 740 / microphone gain 1 / playback gain 0.35 /
source RMS 0.14, tests exactly four positives per language and six negatives,
then restores previous wake and voice settings. The Windows speaker is resolved
by its Misiom-Shooter name. Voice, source gain, board and source hashes must match
before comparison is accepted. Sequential acoustic variation remains a limit.

All large/private artifacts remain excluded from Git. Exact retraining needs
the locally retained K checkpoint, original features and WAV files identified
in `training-config.json` and `training-data-provenance.json`; these metadata
do not claim that hashes alone reconstruct missing training data. Exported
weights permit compilation and numerical reproduction without those artifacts.
