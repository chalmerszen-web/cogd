#include "kws_gru_pcm.h"
#include "kws_buffers.h"
#include <stdalign.h>
#include <string.h>

typedef union {
#ifdef AGENT_KWS_GRU64_RESOURCE_ONLY
    const kws_gru64_model_t *q8;
    const kws_gru64_q6_model_t *q6;
#else
    const kws_gru32_model_t *q8;
    const kws_gru32_q6_model_t *q6;
#endif
} gru_model_t;
struct kws_gru_pcm {
    gru_model_t model;
    const kws_model_t *normalization;
    uint64_t last_sample;
    int16_t previous[256];
    union { kiss_fft_cpx fft_in[512]; uint32_t power[257]; };
    kiss_fft_cpx fft_out[512];
#ifdef AGENT_KWS_GRU64_RESOURCE_ONLY
    kws_gru64_state_t recurrent;
    kws_gru64_scratch_t scratch;
#else
    kws_gru32_state_t recurrent;
    kws_gru32_scratch_t scratch;
#endif
    int8_t input[40];
    int16_t logits[2];
    bool q6;
};
_Static_assert(sizeof(kiss_fft_cpx)==4,"Same fixedQ15 FFT ABI");
size_t kws_gru_pcm_size(void) { return sizeof(kws_gru_pcm_t); }
size_t kws_gru_pcm_alignment(void) { return alignof(kws_gru_pcm_t); }
static int init(void *memory,size_t bytes,const void *model,size_t alignment,
    const kws_model_t *normalization,kws_gru_pcm_t **out)
{
    if(!out) return -1;
    *out=NULL;
    if(!memory || bytes<sizeof(kws_gru_pcm_t) || (uintptr_t)memory%alignof(kws_gru_pcm_t) ||
       !model || (uintptr_t)model%alignment || !normalization || !normalization->mean_q8 || !normalization->inverse_std_q12 ||
       normalization->layers || normalization->trained) return -1;
    kws_gru_pcm_t *s=memory;
    memset(s,0,sizeof(*s));s->normalization=normalization;
    *out=s;return 0;
}
#ifdef AGENT_KWS_GRU64_RESOURCE_ONLY
int kws_gru_pcm_init64(void *memory,size_t bytes,const kws_gru64_model_t *model,
    const kws_model_t *normalization,kws_gru_pcm_t **out)
{
    if(!out) return -1;
    *out=NULL;
    if(!model || (uintptr_t)model%alignof(kws_gru64_model_t) || model->abi!=KWS_GRU64_ABI) return -1;
    if(init(memory,bytes,model,alignof(kws_gru64_model_t),normalization,out)) return -1;
    (*out)->model.q8=model;return 0;
}
int kws_gru_pcm_init64_q6(void *memory,size_t bytes,const kws_gru64_q6_model_t *model,
    const kws_model_t *normalization,kws_gru_pcm_t **out)
{
    if(!out) return -1;
    *out=NULL;
    if(!model || (uintptr_t)model%alignof(kws_gru64_q6_model_t) || model->abi!=KWS_GRU64_Q6_ABI) return -1;
    if(init(memory,bytes,model,alignof(kws_gru64_q6_model_t),normalization,out)) return -1;
    (*out)->model.q6=model;(*out)->q6=true;return 0;
}
#else
int kws_gru_pcm_init(void *memory,size_t bytes,const kws_gru32_model_t *model,
    const kws_model_t *normalization,kws_gru_pcm_t **out)
{
    if(init(memory,bytes,model,alignof(kws_gru32_model_t),normalization,out)) return -1;
    (*out)->model.q8=model;return 0;
}
int kws_gru_pcm_init_q6(void *memory,size_t bytes,const kws_gru32_q6_model_t *model,
    const kws_model_t *normalization,kws_gru_pcm_t **out)
{
    if(!out) return -1;
    *out=NULL;
    if(!model || (uintptr_t)model%alignof(kws_gru32_q6_model_t) || model->abi!=KWS_GRU32_Q6_ABI) return -1;
    if(init(memory,bytes,model,alignof(kws_gru32_q6_model_t),normalization,out)) return -1;
    (*out)->model.q6=model;(*out)->q6=true;return 0;
}
#endif
int kws_gru_pcm_step(kws_gru_pcm_t *s,const int16_t pcm[256],uint64_t end,
    kws_event_t *event,kws_trace_t *trace)
{
    if(!s || !pcm || !event || end<256 || end%256) return -1;
    if(s->last_sample && end-s->last_sample!=256) {
        gru_model_t model=s->model;bool q6=s->q6;
        const kws_model_t *normalization=s->normalization;
        memset(s,0,sizeof(*s));s->model=model;s->q6=q6;s->normalization=normalization;
    }
    s->last_sample=end;
    const kws_frontend_buffers_t buffers={s->previous,s->fft_in,s->fft_out,s->power};
    kws_frontend_buffers_step(&buffers,s->normalization,pcm,s->input,trace);
#ifdef AGENT_KWS_GRU64_RESOURCE_ONLY
    int error=s->q6?kws_gru64_q6_step(s->model.q6,&s->recurrent,s->input,&s->scratch,s->logits):
        kws_gru64_step(s->model.q8,&s->recurrent,s->input,&s->scratch,s->logits);
#else
    int error=s->q6?kws_gru32_q6_step(s->model.q6,&s->recurrent,s->input,&s->scratch,s->logits):
        kws_gru32_step(s->model.q8,&s->recurrent,s->input,&s->scratch,s->logits);
#endif
    if(error) return -1;
    int32_t score=(int32_t)s->logits[1]-s->logits[0];
    *event=(kws_event_t){(int16_t)(score<-32768?-32768:score>32767?32767:score),false,end};
    return 0;
}
int kws_gru_pcm_feed(kws_gru_pcm_t *s,const int16_t pcm[512],uint64_t end,kws_event_t *event)
{
    if(end<512 || end%512) return -1;
    int error=kws_gru_pcm_step(s,pcm,end-256,event,NULL);
    return error?error:kws_gru_pcm_step(s,pcm+256,end,event,NULL);
}
void kws_gru_pcm_values(const kws_gru_pcm_t *s,int16_t hidden[KWS_GRU_PCM_HIDDEN],int16_t logits[2])
{
    if(!s) return;
    if(hidden) memcpy(hidden,s->recurrent.hidden,sizeof(s->recurrent.hidden));
    if(logits) memcpy(logits,s->logits,sizeof(s->logits));
}
