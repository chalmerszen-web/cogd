#ifndef KWS_CTC_H
#define KWS_CTC_H
#include <stdbool.h>
#include <stdint.h>

/* Experimental token ABI. These are trained acoustic classes, not ASR text.
 * Prefix tokens include pronunciation variants; the last syllable keeps tone. */
enum {
    KWS_CTC_BLANK, KWS_CTC_NI, KWS_CTC_HAO, KWS_CTC_XIAO, KWS_CTC_YAN2,
    KWS_CTC_NEI, KWS_CTC_HOU, KWS_CTC_SIU, KWS_CTC_JIN4, KWS_CTC_OTHER,
    KWS_CTC_YAN4, KWS_CTC_YAN1, KWS_CTC_JIN3, KWS_CTC_JIN1,
    KWS_CTC_CLASSES,
    KWS_CTC_FRAME_SAMPLES = 512, KWS_CTC_SPAN_SAMPLES = 48000
};
typedef enum { KWS_CTC_NONE, KWS_CTC_MANDARIN, KWS_CTC_CANTONESE } kws_ctc_match_t;
typedef struct {
    uint64_t last_sample, began[2];
    uint8_t position[2], previous, blanks;
} kws_ctc_t;

void kws_ctc_reset(kws_ctc_t *state);
/* Greedy CTC collapse and a bounded suffix match, not a probability beam.
 * A complete phrase needs two unique-blank frames; blank is a model class,
 * not a demand for physical silence. Tied maxima revoke evidence. Gaps,
 * disarm and invalid inputs cannot carry a partial phrase into another turn.
 * Neural warm-up, confidence calibration and event cooldown belong to the
 * caller. The original scalar wake models cannot supply these token logits. */
kws_ctc_match_t kws_ctc_step(kws_ctc_t *state,
    const int16_t logits[KWS_CTC_CLASSES], uint64_t sample_end, bool armed);
#endif
