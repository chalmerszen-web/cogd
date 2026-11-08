#pragma once
#include <stddef.h>
#include <stdint.h>

#define P4_AOQ_PCM24_MAX_SAMPLES (24000u * 8u)
#define P4_AOQ_PCM16_MAX_SAMPLES (16000u * 8u)

#ifdef __cplusplus
extern "C" {
#endif

/* Offline, whole-buffer mono PCM16: 24000 Hz -> 16000 Hz.
 * Produces floor(samples * 2 / 3) samples with a centered Q15 antialias FIR;
 * samples beyond either endpoint repeat that endpoint. No heap, I/O or state.
 * All counts/capacities are int16_t samples, not bytes. Input is limited to 8 s.
 * Input[0..samples), output[0..capacity) and the written object must be valid,
 * correctly aligned, mutually non-overlapping storage. Empty buffers may be
 * NULL. written itself is required, including for empty input.
 * Returns core agent_err_t numbers: 0 success; 1 invalid pointer/range/overlap;
 * 5 input over limit or insufficient output capacity. On any error, both output
 * and *written remain unchanged. On success *written is the exact sample count.
 * This is a fixture converter, not a streaming microphone resampler. */
int p4_aoq_pcm24_to16(const int16_t* input, size_t samples,
                    int16_t* output, size_t capacity, size_t* written);

#ifdef __cplusplus
}
#endif
