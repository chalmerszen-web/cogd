#ifndef KWS_PHRASE_HEAD_H
#define KWS_PHRASE_HEAD_H

#include "kws.h"

enum { KWS_PHRASE_INPUTS = 24, KWS_PHRASE_CLASSES = 8 };

/* Validate once. Shares the existing final ReLU representation, with no state.
 * Logits use signed Q8. Class0 is the keyword; classes1..7 are near words.
 * Output, if requested, must not alias representation. */
bool kws_phrase_head_valid(const kws_layer_t *layer);
int16_t kws_phrase_head_step(const kws_layer_t *layer,
    const int8_t representation[KWS_PHRASE_INPUTS],
    int16_t logits[KWS_PHRASE_CLASSES]);

#endif
