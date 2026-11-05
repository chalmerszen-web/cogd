#include "kws_temporal_owner.h"
#include <string.h>

void kws_temporal_owner_reset(kws_temporal_owner_t *s, int16_t threshold)
{
    memset(s, 0, sizeof(*s));
    kws_detector_reset(&s->original, threshold);
}

bool kws_temporal_owner_step(kws_temporal_owner_t *s, int16_t original,
                            int16_t verify, uint64_t end, bool armed)
{
    if (!s) return false;
    if (end < 512) { kws_temporal_owner_reset(s, s->original.threshold_q8); return false; }
    bool gap = s->original.blocks && end - s->original.last_sample != 512;
    if (!armed || gap) {
        s->pending = s->last_support = 0;
        s->verify_votes = s->release_blocks = 0;
        s->release_required = false;
    }
    if (s->release_required) {
        if (original < s->original.threshold_q8) {
            if (++s->release_blocks >= (s->original.stable ? 4 : 3)) {
                s->release_required = false;
                s->release_blocks = 0;
            }
        } else s->release_blocks = 0;
    }
    if (s->pending && end - s->pending > KWS_VERIFY_HORIZON) s->pending = 0;
    /* The legacy detector still supplies voting/warm-up. Its automatic
     * cooldown is provisional here: commit only after auxiliary verification.
     * Gap handling inside the detector discards any old accepted cooldown. */
    uint64_t accepted_until = gap ? 0 : s->original.cooldown_until;
    bool candidate = kws_detector_step_armed(&s->original, original, end,
        armed && !s->pending && !s->release_required);
    if (candidate) {
        s->original.cooldown_until = accepted_until;
        s->pending = end;
        s->release_required = true;
        s->release_blocks = 0;
    }
    if (!armed) return false;
    s->verify_votes = (uint8_t)(((s->verify_votes << 1) |
        (verify >= s->original.threshold_q8)) & 7);
    unsigned votes = (s->verify_votes & 1) + ((s->verify_votes >> 1) & 1) +
                     ((s->verify_votes >> 2) & 1);
    if (s->original.blocks >= 64 && votes >= 2) s->last_support = end;
    if (!s->pending || !s->last_support || end - s->last_support > KWS_VERIFY_HORIZON)
        return false;
    s->pending = 0;
    s->original.cooldown_until = end + 24000;
    return true;
}
