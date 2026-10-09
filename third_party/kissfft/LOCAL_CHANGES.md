# Local fixed configuration adaptation

Upstream: KISS FFT 131.1.0, commit `8f47a67f595a6641c566087bf5277034be64f24d`.
The original file hashes remain in `sources.json` under `files`.

Only `_kiss_fft_guts.h` changes: `KISS_FFT_STATIC_512` replaces the one-element
trailing twiddle array with an actual 512-element array. This lets the dedicated
wrapper define a properly typed, const configuration in Flash without aliasing
unrelated structures or relying on an out-of-bounds trailing-array idiom.

`components/kws_c11/kws_fft.c` includes the pinned implementation and calls its
internal `kf_work` using frozen factors `[4,128,4,32,4,8,4,2,2,1]`. The recursive
worker only reads the configuration and factor pointer; the const-removing cast
adapts upstream's API without writing to Flash. Input/output buffers are separate.
No generic-radix, in-place, setup or allocation path executes. Host tests wrap
malloc/calloc/realloc/free and reject any call during frame processing. Upstream
allocation entry points are unused and garbage collected from the firmware.

The modified header is for the dedicated wrapper only. Do not call
`kiss_fft_alloc` with this macro as a general FFT implementation. The firmware
does not export or use that allocation interface.

Q15 butterfly math and rounding are unchanged. Attribution and BSD-3-Clause
license are retained in COPYING and LICENSES/BSD-3-Clause.
