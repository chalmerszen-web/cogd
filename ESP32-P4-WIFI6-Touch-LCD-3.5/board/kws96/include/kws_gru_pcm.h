#ifndef KWS_GRU_PCM_H
#define KWS_GRU_PCM_H
#include "kws.h"
#ifdef AGENT_KWS_GRU64_RESOURCE_ONLY
#include "kws_gru64_q6.h"
#define KWS_GRU_PCM_HIDDEN KWS_GRU64_HIDDEN
#else
#include "kws_gru32_q6.h"
#define KWS_GRU_PCM_HIDDEN KWS_GRU_HIDDEN
#endif

/* Resource-only adapter, with the unchanged40-band frontend. The legacy
 * descriptor supplies only normalization; layers=NULL, trained=false. This
 * adapter NEVER produces a permitted wake event, regardless of its logits. */
typedef struct kws_gru_pcm kws_gru_pcm_t;
#ifdef AGENT_KWS_GRU64_RESOURCE_ONLY
extern const kws_gru64_model_t kws_gru64_probe;
extern const kws_gru64_q6_model_t kws_gru64_q6_probe;
int kws_gru_pcm_init64(void *memory,size_t bytes,const kws_gru64_model_t *model,
    const kws_model_t *normalization,kws_gru_pcm_t **out);
int kws_gru_pcm_init64_q6(void *memory,size_t bytes,const kws_gru64_q6_model_t *model,
    const kws_model_t *normalization,kws_gru_pcm_t **out);
#else
extern const kws_gru32_model_t kws_gru32_probe;
extern const kws_gru32_q6_model_t kws_gru32_q6_probe;
int kws_gru_pcm_init(void *memory,size_t bytes,const kws_gru32_model_t *model,
    const kws_model_t *normalization,kws_gru_pcm_t **out);
int kws_gru_pcm_init_q6(void *memory,size_t bytes,const kws_gru32_q6_model_t *model,
    const kws_model_t *normalization,kws_gru_pcm_t **out);
#endif
size_t kws_gru_pcm_size(void);
size_t kws_gru_pcm_alignment(void);
int kws_gru_pcm_step(kws_gru_pcm_t *state,const int16_t pcm[256],uint64_t end,
    kws_event_t *event,kws_trace_t *trace);
int kws_gru_pcm_feed(kws_gru_pcm_t *state,const int16_t pcm[512],uint64_t end,kws_event_t *event);
void kws_gru_pcm_values(const kws_gru_pcm_t *state,int16_t hidden[KWS_GRU_PCM_HIDDEN],int16_t logits[2]);
#endif
