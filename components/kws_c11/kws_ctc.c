#include "kws_ctc.h"
#include <string.h>

static const uint8_t phrase[2][4] = {
    {KWS_CTC_NI, KWS_CTC_HAO, KWS_CTC_XIAO, KWS_CTC_YAN2},
    {KWS_CTC_NEI, KWS_CTC_HOU, KWS_CTC_SIU, KWS_CTC_JIN4}
};

void kws_ctc_reset(kws_ctc_t *s)
{ if (s) memset(s, 0, sizeof(*s)); }

static void clear_phrase(kws_ctc_t *s)
{
    s->began[0] = s->began[1] = 0;
    s->position[0] = s->position[1] = 0;
    s->previous = KWS_CTC_BLANK;
    s->blanks = 0;
}

kws_ctc_match_t kws_ctc_step(kws_ctc_t *s,
    const int16_t logits[KWS_CTC_CLASSES], uint64_t end, bool armed)
{
    if (!s) return KWS_CTC_NONE;
    if (!logits || end < KWS_CTC_FRAME_SAMPLES || end % KWS_CTC_FRAME_SAMPLES) {
        kws_ctc_reset(s);
        return KWS_CTC_NONE;
    }
    if (s->last_sample && end - s->last_sample != KWS_CTC_FRAME_SAMPLES)
        clear_phrase(s);
    s->last_sample = end;
    if (!armed) { clear_phrase(s); return KWS_CTC_NONE; }
    for (unsigned p = 0; p < 2; ++p)
        if (s->position[p] && end - s->began[p] > KWS_CTC_SPAN_SAMPLES) {
            s->position[p] = 0;
            s->began[p] = 0;
        }
    uint8_t token = KWS_CTC_BLANK;
    bool tied = false;
    int16_t best = logits[0];
    for (unsigned i = 1; i < KWS_CTC_CLASSES; ++i) {
        if (logits[i] > best) { best = logits[i]; token = (uint8_t)i; tied = false; }
        else if (logits[i] == best) tied = true;
    }
    if (tied) token = KWS_CTC_OTHER;
    if (token == KWS_CTC_BLANK) {
        s->previous = token;
        if (s->blanks < 2) ++s->blanks;
        if (s->blanks == 2) for (unsigned p = 0; p < 2; ++p)
            if (s->position[p] == 4) {
                clear_phrase(s);
                return p == 0 ? KWS_CTC_MANDARIN : KWS_CTC_CANTONESE;
            }
        return KWS_CTC_NONE;
    }
    s->blanks = 0;
    if (token == s->previous) return KWS_CTC_NONE;
    s->previous = token;
    for (unsigned p = 0; p < 2; ++p) {
        unsigned position = s->position[p];
        if (position < 4 && token == phrase[p][position]) {
            if (!position) s->began[p] = end;
            ++s->position[p];
        } else {
            s->position[p] = token == phrase[p][0] ? 1 : 0;
            s->began[p] = s->position[p] ? end : 0;
        }
    }
    return KWS_CTC_NONE;
}
