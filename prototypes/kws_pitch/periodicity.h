#ifndef KWS_PERIODICITY_H
#define KWS_PERIODICITY_H

#include "pitch.h"

typedef kws_pitch_t kws_periodicity_t;

typedef struct {
    int16_t trusted_frequency_q4; /* Original YIN frequency;0 stays unknown. */
    int16_t strength_q12; /* Continuous periodic strength, NOT a voiced decision. */
} kws_periodicity_features_t;

void kws_periodicity_reset(kws_periodicity_t *state);
/* Standalone optional prototype: same past-only256 samples and state as YIN.
 * Trusted outputs remain exact. Unknown frequency never receives a weak guess.
 */
kws_periodicity_features_t kws_periodicity_block(kws_periodicity_t *state,
                                                const int16_t pcm[KWS_PITCH_BLOCK]);

#endif
