#ifndef KWS_VERIFIED_H
#define KWS_VERIFIED_H
#include "kws.h"

typedef struct kws_verified kws_verified_t;
size_t kws_verified_size(void);
size_t kws_verified_alignment(void);
/* Registered24 E/L plus registered48 verifier, with identical normalization. */
int kws_verified_init(void *memory, size_t bytes, const kws_model_t *first,
    const kws_model_t *second, const kws_model_t *verifier, kws_verified_t **out);
void kws_verified_reset(kws_verified_t *state);
void kws_verified_threshold(kws_verified_t *state, int16_t threshold);
void kws_verified_stability(kws_verified_t *state, bool stable);
/* Fresh init only. Hash-bound128 zero-PCM frames;0 when no matching seed. */
uint64_t kws_verified_prime(kws_verified_t *state);
/* Trace contains the first24 model's265 meaningful layer values in the48
 * capacity trace. score_q8 is the current original E/L smoothed score;
 * detected may confirm an earlier owned event within the fixed256ms window. */
int kws_verified_step_pcm_armed(kws_verified_t *state, const int16_t pcm[256],
    uint64_t end, bool armed, kws_event_t *event, kws_trace_t *trace);
int kws_verified_step_features_armed(kws_verified_t *state, const int8_t input[40],
    uint64_t end, bool armed, kws_event_t *event, kws_trace_t *trace);
int kws_verified_feed_512_armed(kws_verified_t *state, const int16_t pcm[512],
    uint64_t end, bool armed, kws_event_t *event);
void kws_verified_scores(const kws_verified_t *state, int16_t heads[3], int16_t filtered[2]);
#endif
