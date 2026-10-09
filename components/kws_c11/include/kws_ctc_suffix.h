#ifndef KWS_CTC_SUFFIX_H
#define KWS_CTC_SUFFIX_H
#include "kws_ctc.h"

#define KWS_CTC_BEAM 8
#define KWS_CTC_NEXT (KWS_CTC_BEAM * KWS_CTC_CLASSES)

/* Internal fixed storage is public only to allow caller-owned static memory.
 * Probabilities and best alignments have independent Q16 normalizers. */
typedef struct {
    uint8_t token[4], length, age[2][4];
    int32_t mass[2], path[2];
} kws_ctc_suffix_node_t;

typedef struct {
    kws_ctc_suffix_node_t beam[KWS_CTC_BEAM], next[KWS_CTC_NEXT];
    uint64_t last_sample;
    uint8_t count, quiet, fired[4], fired_length;
} kws_ctc_suffix_t;

void kws_ctc_suffix_reset(kws_ctc_suffix_t *state);
/* Continuous all-class finite-tail beam. Age is the best-path alignment,
 * not an assertion about every summed path or calibrated confidence.
 * Gaps/disarm/invalid clocks/all-class flat scores clear evidence;
 * no heap or bounded input reset. */
kws_ctc_match_t kws_ctc_suffix_step(kws_ctc_suffix_t *state,
    const int16_t logits[KWS_CTC_CLASSES], uint64_t sample_end, bool armed);
#endif
