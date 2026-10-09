#include "kws_owner.h"
#include <string.h>

void kws_owner_reset(kws_owner_t *s, int16_t threshold)
{
    memset(s, 0, sizeof(*s));
    kws_detector_reset(&s->original, threshold);
}

bool kws_owner_step(kws_owner_t *s, int16_t original, int16_t verify,
                    uint64_t end, bool armed)
{
    if (!s) return false;
    if (end < 512) { kws_owner_reset(s, s->original.threshold_q8); return false; }
    if (!armed || (s->original.blocks && end - s->original.last_sample != 512))
        s->verify_votes = 0;
    if (armed)
        s->verify_votes = (uint8_t)(((s->verify_votes << 1) |
            (verify >= s->original.threshold_q8)) & 7);
    unsigned votes = (s->verify_votes & 1) + ((s->verify_votes >> 1) & 1) +
                     ((s->verify_votes >> 2) & 1);
    return kws_detector_step_armed(&s->original, original, end, armed) && votes >= 2;
}
