#ifndef KWS_OWNER_H
#define KWS_OWNER_H
#include "kws.h"

typedef struct {
    kws_detector_t original;
    uint8_t verify_votes;
} kws_owner_t;

void kws_owner_reset(kws_owner_t *state, int16_t threshold_q8);
/* An output is an accepted ORIGINAL event at that same timestamp, gated
 * by independent two-of-three verification evidence. The verifier has no
 * event cooldown; an earlier high score cannot suppress later evidence.
 * Caller supplies the independently smoothed logits of both branches.
 * Currently experimental: not included by the default firmware build. */
bool kws_owner_step(kws_owner_t *state, int16_t original, int16_t verify,
                    uint64_t sample_end, bool armed);
#endif
