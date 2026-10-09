/* Original fixed-point YIN-style feature prototype, not a wake detector.
 * Difference function, cumulative normalization, first subthreshold trough,
 * then parabolic period refinement. No upstream implementation is copied.
 * Reference: de Cheveigne/Kawahara, JASA111(4),1917-1930,2002.
 */
#include "pitch.h"
#include <string.h>

enum { HALF_BLOCK = KWS_PITCH_BLOCK/2, WIDTH = 256, MIN_LAG = 10,
       MAX_LAG = 100, THRESHOLD_Q15 = 4915 };

void kws_pitch_reset(kws_pitch_t *state) {
    memset(state, 0, sizeof(*state));
}

kws_pitch_features_t kws_pitch_block(kws_pitch_t *state,
                                    const int16_t pcm[KWS_PITCH_BLOCK]) {
    kws_pitch_features_t result = {0, 0};
    memmove(state->window, state->window+HALF_BLOCK,
            (KWS_PITCH_WINDOW-HALF_BLOCK)*sizeof(state->window[0]));
    for (unsigned i = 0; i < HALF_BLOCK; ++i) {
        int32_t pair = (int32_t)pcm[2*i]+pcm[2*i+1];
        state->window[KWS_PITCH_WINDOW-HALF_BLOCK+i] = (int16_t)(pair/32);
    }
    if (state->seen < KWS_PITCH_WINDOW) {
        state->seen = (uint16_t)(state->seen+HALF_BLOCK);
        if (state->seen < KWS_PITCH_WINDOW) return result;
    }
    int64_t sum = 0;
    uint64_t energy = 0;
    for (unsigned i = 0; i < WIDTH; ++i) {
        int32_t value = state->window[i];
        sum += value;
        energy += (uint32_t)(value*value);
    }
    /* RMS>=64 raw ADC units after removing DC; prevents silence estimates. */
    if (energy-(uint64_t)(sum*sum)/WIDTH < WIDTH*16u) return result;
    uint64_t cumulative = 0;
    int32_t previous = 32768, before = 32768;
    for (unsigned lag = 1; lag <= MAX_LAG+1; ++lag) {
        uint64_t difference = 0;
        for (unsigned i = 0; i < WIDTH; ++i) {
            int32_t delta = (int32_t)state->window[i]-state->window[i+lag];
            difference += (uint32_t)(delta*delta);
        }
        cumulative += difference;
        if (!cumulative) continue;
        uint64_t scaled = (difference*lag*32768u+cumulative/2)/cumulative;
        int32_t current = (int32_t)(scaled > 131072 ? 131072 : scaled);
        if (lag > MIN_LAG && previous < THRESHOLD_Q15 &&
            previous < before && previous <= current) {
            int32_t denominator = 2*(before-2*previous+current);
            int32_t offset = denominator > 0 ? (before-current)*256/denominator : 0;
            if (offset < -128) offset = -128;
            if (offset > 128) offset = 128;
            int32_t period = (int32_t)(lag-1)*256+offset;
            if (period < MIN_LAG*256) period = MIN_LAG*256;
            if (period > MAX_LAG*256) period = MAX_LAG*256;
            result.frequency_q4 = (int16_t)((32768000u+(uint32_t)period/2)/(uint32_t)period);
            result.periodicity_q12 = (int16_t)(4096-(previous+4)/8);
            return result;
        }
        before = previous;
        previous = current;
    }
    return result;
}
