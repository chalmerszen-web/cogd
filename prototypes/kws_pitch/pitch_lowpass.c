#include "pitch_lowpass.h"
#include <string.h>

enum { BETA_Q15 = 12313 }; /* round((1-exp(-2*pi*1200/16000))*32768) */

void kws_pitch_lowpass_reset(kws_pitch_lowpass_t *state)
{
    if (state) memset(state, 0, sizeof(*state));
}

bool kws_pitch_lowpass_filter(kws_pitch_lowpass_t *state,
                             const int16_t pcm[KWS_PITCH_BLOCK],
                             int16_t filtered[KWS_PITCH_BLOCK])
{
    if (!state || !pcm || !filtered) return false;
    for (unsigned i = 0; i < KWS_PITCH_BLOCK; ++i) {
        int32_t value = (int32_t)pcm[i] * 2;
        for (unsigned section = 0; section < 2; ++section) {
            int32_t previous = state->section_q1[section];
            /* Convex update preserves the PCM Q1 range. The largest product
             * is131070*12313=1613864910, below INT32_MAX; no64-bit multiply.
             */
            value = previous + (value - previous) * BETA_Q15 / 32768;
            state->section_q1[section] = value;
        }
        filtered[i] = (int16_t)(value / 2);
    }
    return true;
}

kws_pitch_features_t kws_pitch_lowpass_block(kws_pitch_lowpass_t *state,
                                            const int16_t pcm[KWS_PITCH_BLOCK])
{
    int16_t filtered[KWS_PITCH_BLOCK];
    if (!kws_pitch_lowpass_filter(state, pcm, filtered))
        return (kws_pitch_features_t){0, 0};
    return kws_pitch_block(&state->pitch, filtered);
}
