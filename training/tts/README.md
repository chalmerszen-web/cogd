# Local speech preparation for the Phase 2 pilot

No remote inference or private recordings are used. TTS runs on the computer;
these dependencies are NOT linked into ESP32 firmware. Large downloads and WAVs
live in ignored `artifacts/kws-phase2`. KWS training retains its separate
PyTorch 2.6.0 environment.

## Sources

- CosyVoice source: ASLP-lab/WenetSpeech-Yue commit
  `bea884c67f03f73f2d2d94557457a5849dd61090`, `CosyVoice2-Yue/` subtree.
- Mandarin: FunAudioLLM/CosyVoice2-0.5B revision
  `eec1ae6c79877dbd9379285cf8789c9e0879293d` (model card Apache-2.0).
- Cantonese: ASLP-lab/Cosyvoice2-Yue revision
  `b6d0eb0b4b594c67100e8786fe97227042d451a9` (model card Apache-2.0).
- Synthetic reference voices: hexgrad/Kokoro-82M revision
  `f3ff3571791e39611d31c381e3a41a3af07b4987` (model card Apache-2.0).
  Eight provided Chinese voice presets, no uploaded/private voice references.
- Natural negative speech: google/fleurs revision
  `70bb2e84b976b7e960aa89f1c648e09c59f894dd`, CC BY 4.0.
  `fleurs_subset.py` saves attribution and upstream README. Only train and test
  splits are used, respecting the documented train versus dev/test speaker
  separation. Individual speaker identities are not exposed by this metadata.
- Automatic pronunciation screening only: SenseVoiceSmall via sherpa-onnx
  1.12.26, conversion revision `2365baeacb507f821a0c8120fcee3d484dba7a07`.
  This model uses the FunASR model license, not Apache-2.0. See saved LICENSE
  and upstream https://github.com/modelscope/FunASR/blob/main/MODEL_LICENSE.
  It supplies screening transcripts, not KWS weights or a human listening score.

`lock_sources.py` records source URLs, revisions, sizes and downloaded SHA256.
Synthetic reference identities are split BEFORE generation: four train voices,
two validation voices and two test voices. Both languages and all descendants
of each identity stay in that split. Different random seeds/speeds are NOT
different speakers. Two CosyVoice variants are not independent TTS families.

## Environment

Create a dedicated Python 3.11 (Windows) or 3.12 (WSL) virtual environment.
Install Torch 2.3.1 and torchaudio 2.3.1 from the official cu121 wheel index,
with the ordinary PyPI index available for their dependencies. Then install
setuptools 75.8.0, wheel, cython, numpy 1.26.4 and this directory's
`requirements.txt` using `--no-build-isolation` (the old Whisper setup imports
pkg_resources). Run `pip check` and save a full environment freeze.

The tested WSL instance exposes no `/dev/dxg` or NVIDIA driver. CPU generation
works there; the separate Windows environment sees the RTX 2060. No system GPU
driver changes are needed. Torch/ESP-IDF environments are kept separate.

The pinned CosyVoice text normalizer attempts a model download even for calls
using `text_frontend=False`. `patch_source.py` applies and records a narrow
offline patch: under `COGD_TTS_PRENORMALIZED=1`, skip unused normalizer creation.
All project generation calls explicitly use `text_frontend=False` with literal
normalized Chinese text. No TensorRT, DeepSpeed, vLLM or TTS training is needed.

Run source patching and `generate.py references` first. Next run small `pilot`
generation for each language and `audit_audio.py`; do not generate the full
dataset until this quality gate is reviewed. `yue-references` can generate
Cantonese synthetic reference sentences from the same source identities, then
`--cantonese-reference` selects zero-shot synthesis with that reference text.
This remains a source descendant, not a new independent voice.

ASR may spell homophones as 小严/小妍/小颜. This alone does not mean an acoustic
error. Conversely missing syllables, wrong language or overlong speech must not
be silently accepted. The generator stores approximate energy boundaries,
duration and whether the 2.048-second receptive field is exceeded. Long
positives are rejected by the KWS manifest reader rather than word-truncated.
Human pronunciation/generalization has not been verified by automatic ASR.

The bounded Yue dataset uses the exact Cantonese homophone `你好小賢` followed
by a carrier sentence, then extracts and screens the four-syllable wake phrase.
The user-facing wake word remains `你好，小言`. 言/賢 are both jin4; 燕 (jin3),
嚴 (jim4), 顏 (ngaan4) are not accepted substitutes in Cantonese. The corpus
retains the literal synthesis text and this distinction; there is no fuzzy
character-distance acceptance rule. Reference: CUHK Cantonese Lexicon,
https://humanum.arts.cuhk.edu.hk/Lexis/lexi-can/pho-rel.php?s1=j&s2=in&s3=4

`backgrounds.py --out <new directory>` adds 48 independently seeded procedural
noise/hum/music recordings (480 seconds). These are synthetic negative examples,
not a claim of real television/music coverage. Original composition seeds stay
in one split. Natural speech negatives retain the separate FLEURS license.

## Final reserved sources and conversion checks

`lock_sources.py --reserved-voices` additionally downloads af_heart and
am_michael from the same pinned Kokoro revision. `generate.py` accepts explicit
`--voices af_heart am_michael`; references, Yue references, pilot and dataset
generation retain those source identities. They are reserved for the final test
after the original two test identities were retired into development. They are
not a new TTS family or proven independent human speakers.

The fixed final batch uses `--test-count 150 --variant final1` per language.
For Yue also use `--cantonese-reference --carrier --phonetic-alias --speed 1.15`.
The curation pass finds a unique exact target span in the carrier, then requires
the isolated crop to decode as the complete target. It rejects ambiguous spans,
incorrect syllables and overlong clips; it does not simply strip the first four
characters. Pilot and rejected recordings remain separate evidence.

The first FLEURS conversion was rejected before training: direct FLOAT WAV to
int16 reading omitted amplitude scaling. `fleurs_subset.py` now reads float,
scales/clips to PCM16 and rejects effectively silent speech while preserving
the original WAV, subtype, hashes and peak. Correct files are in `fleurs-v2`.
Focused regression checks cover normalized float, integer PCM and silent input
in `tests/tts/test_fleurs.py`; target localization is checked in
`tests/tts/test_curation.py`.
