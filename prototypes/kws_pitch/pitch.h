#ifndef KWS_PITCH_H
#define KWS_PITCH_H

#include <stdint.h>

enum { KWS_PITCH_BLOCK = 256, KWS_PITCH_WINDOW = 384 };

typedef struct {
    int16_t window[KWS_PITCH_WINDOW]; /* 8 kHz, PCM divided by16. */
    uint16_t seen;
} kws_pitch_t;

typedef struct {
    int16_t frequency_q4; /* Hz times16, zero when unvoiced. */
    int16_t periodicity_q12;
} kws_pitch_features_t;

void kws_pitch_reset(kws_pitch_t *state);
/* Exactly256 mono16kHz samples;48ms past-only window, no heap or globals. */
kws_pitch_features_t kws_pitch_block(kws_pitch_t *state,
                                    const int16_t pcm[KWS_PITCH_BLOCK]);

#endif
