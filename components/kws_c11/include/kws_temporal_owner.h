#ifndef KWS_TEMPORAL_OWNER_H
#define KWS_TEMPORAL_OWNER_H
#include "kws.h"

enum { KWS_VERIFY_HORIZON = 4096 }; /* 256 ms, not a tunable threshold. */
typedef struct {
    kws_detector_t original;
    uint64_t pending, last_support;
    uint8_t verify_votes, release_blocks;
    bool release_required;
} kws_temporal_owner_t;

void kws_temporal_owner_reset(kws_temporal_owner_t *state, int16_t threshold_q8);
/* Each output consumes one primary candidate. Verification may precede
 * or follow its anchor by at most256 ms. Output is causal and never earlier
 * than its owner. Only a final accepted output starts the1.5s cooldown.
 * One immutable pending window is allowed; another candidate also requires
 * three consecutive below-threshold primary blocks (four in stable mode).
 * Disarm clears pending/support/release evidence but preserves accepted
 * cooldown. Clock gaps reset all temporal state and repeat warm-up. */
bool kws_temporal_owner_step(kws_temporal_owner_t *state, int16_t original,
                            int16_t verify, uint64_t end, bool armed);
#endif
