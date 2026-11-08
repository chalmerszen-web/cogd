# P4 offline PCM fixture conversion

The C-compatible `p4_aoq_pcm24_to16` helper converts a whole mono PCM16 buffer
from 24 kHz to 16 kHz. It uses no heap, floating-point operations, I/O or global
mutable state. This is intended for a bounded on-board TTS-to-ASR fixture, not
continuous microphone processing. Input is at most 192000 samples (8 seconds);
output is exactly `floor(N * 2 / 3)`, at most 128000 samples.

Counts and capacities are samples, not bytes. Input, the full declared output
capacity, and `written` must be disjoint and aligned. Empty buffers may be NULL;
`written` is always required. Returns 0 on success, 1 for invalid arguments,
address overflow or overlap, and 5 for input/capacity limits. Failure changes
neither output nor `*written`; initialize `written = 0` before calling if needed.

## Filter design

Interpolate by 2 to 48 kHz, apply a 97-tap symmetric low-pass windowed sinc with
7 kHz cutoff and Kaiser beta 8.6, then decimate by 3. The checked-in table has
two 49-tap Q15 phases (one phase has an unused zero outer tap). The generator
normalizes each phase to exactly 32768 for exact DC preservation, then adjusts
the center coefficient/pair to retain symmetry after quantization. No scipy or
external package is needed to reproduce the table.

The whole-buffer API centers the FIR on output time `m * 3 / 2` in input sample
units. Samples before/after the buffer repeat the nearest endpoint; there is no
zero padding, emitted tail or timing shift. Products fit in int32; the sum is
int64, rounded symmetrically and saturated to PCM16. The output loses frequencies
near and above the new Nyquist limit by design.

## Reproduce validation

From the repository root on Linux/WSL (GCC, Python 3):

```sh
bash tests/aoq_pcm_fixture/run.sh
```

From PowerShell:

```powershell
wsl.exe -d Ubuntu -- bash tests/aoq_pcm_fixture/run.sh
python tests/aoq_pcm_fixture/compile_p4.py
```

The first command runs the actual helper with ASan/UBSan and `-Wall -Wextra
-Werror`, verifies the header also compiles as C11, and regenerates all 98 Q15
coefficients for exact comparison. It checks exact DC including full-scale and
extended endpoints, lengths 0..103 and the 8-second maximum, capacity sentinels,
null/overflow/alignment/overlap rejection without output changes, signed
saturation, a 1 kHz tone within 0.2 Hz and 0.5% gain, and 10 kHz alias suppression
of at least 60 dB. Tone measurements exclude 64 output samples at each edge.

The second command only compiles this one C++ file using the existing P4
`build-aoq-native/compile_commands.json` flags for `aoq_probe.cpp`. It does not
reconfigure or link firmware. Logs, source/header hashes and object hash go to
`.local/aoq-native-20261107/pcm-fixture/` (optional first argument overrides it).
No hardware, cloud, ASR accuracy, real-time performance or media interoperability
claim follows from these tests.
