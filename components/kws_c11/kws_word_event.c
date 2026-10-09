#include "kws_word_event.h"
#include "kws_ctc_math.h"
#include <string.h>

void kws_word_event_reset(kws_word_event_t *s)
{ if (s) memset(s,0,sizeof(*s)); }

bool kws_word_event_step(kws_word_event_t *s,const int16_t scores[2],uint64_t end,bool armed)
{
    if (!s) return false;
    if (!scores || end < 512 || end % 512) { kws_word_event_reset(s);return false; }
    if (s->last_sample && (end <= s->last_sample || end-s->last_sample != 512))
        kws_word_event_reset(s);
    s->last_sample = end;
    if (!armed) { kws_word_event_reset(s);s->last_sample = end;return false; }
    memcpy(s->history[s->next],scores,sizeof(s->history[0]));
    s->next = (s->next+1)%KWS_WORD_WINDOW;
    if (s->count < KWS_WORD_WINDOW) ++s->count;
    s->quiet = scores[0] > scores[1] ? s->quiet < 2 ? s->quiet+1 : 2 : 0;
    int32_t before = 0,word = KWS_LOG_ABSENT,after = KWS_LOG_ABSENT,total = 0;
    unsigned oldest = s->count == KWS_WORD_WINDOW ? s->next : 0;
    for (unsigned i = 0; i < s->count; ++i) {
        const int16_t *frame = s->history[(oldest+i)%KWS_WORD_WINDOW];
        int32_t blank_score = (int32_t)frame[0]*256,wake_score = (int32_t)frame[1]*256;
        after = kws_log_score_add(kws_log_add(word,after),blank_score);
        word = kws_log_score_add(kws_log_add(before,word),wake_score);
        before = kws_log_score_add(before,blank_score);
        total = kws_log_score_add(total,kws_log_add(blank_score,wake_score));
    }
    /* round(Q16*log(1/2)); ties are rejected. */
    bool majority = (int64_t)kws_log_add(word,after)-total > -45426;
    if (!majority) s->fired = false;
    if (majority && s->quiet == 2 && !s->fired) { s->fired = true;return true; }
    return false;
}
