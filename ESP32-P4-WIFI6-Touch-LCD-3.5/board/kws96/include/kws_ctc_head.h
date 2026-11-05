#ifndef KWS_CTC_HEAD_H
#define KWS_CTC_HEAD_H
#include "kws.h"
#include "kws_ctc.h"

/* Separate descriptor: the legacy one-output layer cannot masquerade as a
 * trained token head. Width is fixed by this library's24/48-channel ABI. */
bool kws_ctc_head_valid(const kws_layer_t *layer);
int kws_ctc_head_step(const kws_layer_t *layer, const int8_t *representation,
                     int16_t logits[KWS_CTC_CLASSES]);
#endif
