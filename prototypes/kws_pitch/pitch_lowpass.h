#ifndef KWS_PITCH_LOWPASS_H
#define KWS_PITCH_LOWPASS_H

#include "pitch.h"
#include <stdbool.h>

/* Optional prototype. Normal firmware and the original pitch ABI do not use it. */
typedef struct {
    kws_pitch_t pitch;
    int32_t section_q1[2];
} kws_pitch_lowpass_t;

void kws_pitch_lowpass_reset(kws_pitch_lowpass_t *state);
/* Exactly256 samples at16kHz. Buffers may be identical or disjoint.
 * Q1 states stay in[-65536,65534]; reset before first use. No heap.
 */
bool kws_pitch_lowpass_filter(kws_pitch_lowpass_t *state,
                             const int16_t pcm[KWS_PITCH_BLOCK],
                             int16_t filtered[KWS_PITCH_BLOCK]);
kws_pitch_features_t kws_pitch_lowpass_block(kws_pitch_lowpass_t *state,
                                            const int16_t pcm[KWS_PITCH_BLOCK]);

#endif
