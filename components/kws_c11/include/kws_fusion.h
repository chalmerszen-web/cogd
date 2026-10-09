#ifndef KWS_FUSION_H
#define KWS_FUSION_H
#include "kws.h"
typedef struct kws_fusion kws_fusion_t;
size_t kws_fusion_size(void);
size_t kws_fusion_alignment(void);
int kws_fusion_init(void *memory,size_t bytes,const kws_model_t *first,const kws_model_t *second,kws_fusion_t **out);
/* Immediately after init, before setting threshold: restore exactly 128
 * zero-PCM frames for the hash-bound E/L build, or leave other models intact.
 * Returns the synthetic sample clock. Next feed_512 ends at return+512. */
uint64_t kws_fusion_prime(kws_fusion_t *state);
void kws_fusion_reset(kws_fusion_t *state);
void kws_fusion_threshold(kws_fusion_t *state,int16_t threshold);
void kws_fusion_stability(kws_fusion_t *state,bool stable);
/* One shared frontend; equal INT16 logits rounded toward zero, then a causal
 * three-block mean. Trace remains the first model for existing parity tools. */
int kws_fusion_step_pcm(kws_fusion_t *state,const int16_t pcm[256],uint64_t end,kws_event_t *event,kws_trace_t *trace);
int kws_fusion_feed_512(kws_fusion_t *state,const int16_t pcm[512],uint64_t end,kws_event_t *event);
int kws_fusion_step_pcm_armed(kws_fusion_t *state,const int16_t pcm[256],uint64_t end,bool armed,kws_event_t *event,kws_trace_t *trace);
int kws_fusion_feed_512_armed(kws_fusion_t *state,const int16_t pcm[512],uint64_t end,bool armed,kws_event_t *event);
void kws_fusion_scores(const kws_fusion_t *state,int16_t scores[2]);
#ifdef AGENT_KWS_FUSION
extern const kws_model_t kws_secondary_model;
#endif
#endif
