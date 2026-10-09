#ifndef KWS_CTC_RUNTIME_H
#define KWS_CTC_RUNTIME_H
#include "kws_ctc_head.h"

typedef struct kws_ctc_stream kws_ctc_stream_t;
typedef struct {
    const kws_model_t *features; /* Valid backbone with unused untrained scalar. */
    const kws_layer_t *tokens;
    bool trained; /* Explicit authority of this separate acoustic token model. */
} kws_ctc_model_t;
typedef struct {
    int16_t logits[KWS_CTC_CLASSES];
    kws_ctc_match_t phrase;
    uint64_t sample_end;
} kws_ctc_output_t;

size_t kws_ctc_stream_size(void);
size_t kws_ctc_stream_alignment(void);
int kws_ctc_stream_init(void *memory, size_t bytes,
    const kws_ctc_model_t *model, kws_ctc_stream_t **out);
/* Model-derived steady zero-PCM context, not zero normalized features.
 * No delayed warm-up, stale token evidence or dynamic allocation. */
void kws_ctc_stream_reset(kws_ctc_stream_t *stream);
/* One mono PCM16/16kHz block. Gaps and rearm start a fresh primed context.
 * Disarm clears evidence and skips FFT/CNN. The caller owns audio exclusion,
 * event cooldown, cancellation and the lifetime of immutable model data. */
int kws_ctc_stream_feed(kws_ctc_stream_t *stream, const int16_t pcm[512],
    uint64_t sample_end, bool armed, kws_ctc_output_t *out);
#endif
