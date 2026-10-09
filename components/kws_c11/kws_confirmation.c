#include "kws_confirmation.h"
#include <string.h>

void kws_confirmation_reset(kws_confirmation_t *s)
{ memset(s, 0, sizeof(*s)); }

bool kws_confirmation_step(kws_confirmation_t *s, bool first, bool second,
                           uint64_t end, bool armed)
{
    if (!s) return false;
    if (end < 512 || (s->last_sample &&
        (end <= s->last_sample || end - s->last_sample != 512)))
        kws_confirmation_reset(s);
    s->last_sample = end;
    if (!armed || end < 512) { s->mask = 0; return false; }
    for (unsigned i = 0; i < 2; ++i)
        if ((s->mask & (1u << i)) && end - s->pending[i] > KWS_CONFIRMATION_GAP)
            s->mask &= (uint8_t)~(1u << i);
    if (first) { s->pending[0] = end; s->mask |= 1; }
    if (second) { s->pending[1] = end; s->mask |= 2; }
    if (s->mask != 3) return false;
    s->mask = 0;
    return true;
}
