#ifndef KWS_WORD_EVENT_H
#define KWS_WORD_EVENT_H
#include <stdbool.h>
#include <stdint.h>

#define KWS_WORD_WINDOW 8
typedef struct {
    int16_t history[KWS_WORD_WINDOW][2];
    uint64_t last_sample;
    uint8_t count, next, quiet;
    bool fired;
} kws_word_event_t;

void kws_word_event_reset(kws_word_event_t *state);
/* Scores are [no wake event, whole bilingual wake]. No phoneme claims.
 * Exact-one-event posterior over the last<=8 frames must exceed1/2,
 * followed by two unique blank winners. No input-length reset or heap. */
bool kws_word_event_step(kws_word_event_t *state, const int16_t scores[2],
    uint64_t sample_end, bool armed);
#endif
