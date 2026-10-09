#ifndef KWS_TEMPORAL_OWNER_H
#define KWS_TEMPORAL_OWNER_H
#include "kws.h"

enum { KWS_VERIFY_HORIZON = 4096 }; /* 256 ms, not a tunable threshold. */
typedef struct {
    kws_detector_t original;
    uint64_t pending, last_support;
    uint8_t verify_votes;
} kws_temporal_owner_t;

void kws_temporal_owner_reset(kws_temporal_owner_t *state, int16_t threshold_q8);
/* Each output consumes one original accepted event. Verification may precede
 * or follow its anchor by at most256 ms. Output is causal and never earlier
 * than its owner. Original warm-up/cooldown remain; auxiliary evidence has no
 * event cooldown. Gaps and disarm clear all pending/support evidence.
 * Final spacing can be1.5s-256ms; it is NOT another1.5s output cooldown. */
bool kws_temporal_owner_step(kws_temporal_owner_t *state, int16_t original,
                            int16_t verify, uint64_t end, bool armed);
#endif
