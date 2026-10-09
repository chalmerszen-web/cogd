#include "kws_gru_pcm.h"
#include "kws_buffers.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

#ifdef AGENT_KWS_GRU64_RESOURCE_ONLY
#ifdef AGENT_KWS_GRU_Q6_RESOURCE_ONLY
#define PROBE kws_gru64_q6_probe
#define INIT kws_gru_pcm_init64_q6
typedef kws_gru64_q6_model_t probe_model_t;
#else
#define PROBE kws_gru64_probe
#define INIT kws_gru_pcm_init64
typedef kws_gru64_model_t probe_model_t;
#endif
#else
#define PROBE kws_gru32_probe
#define INIT kws_gru_pcm_init
typedef kws_gru32_model_t probe_model_t;
#endif

#ifdef PROBE_HEAP_WRAPPERS
static unsigned heap_calls;
extern void *__real_malloc(size_t);
extern void *__real_calloc(size_t,size_t);
extern void *__real_realloc(void *,size_t);
extern void __real_free(void *);
void *__wrap_malloc(size_t n) { ++heap_calls;return __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t bytes) { ++heap_calls;return __real_calloc(n,bytes); }
void *__wrap_realloc(void *p,size_t n) { ++heap_calls;return __real_realloc(p,n); }
void __wrap_free(void *p) { ++heap_calls;__real_free(p); }
#endif

int main(void)
{
    alignas(max_align_t) unsigned char memory[8192], other[8192], fresh[8192];
    kws_gru_pcm_t *a, *b, *c;
    size_t bytes=kws_gru_pcm_size();
    assert(bytes<=sizeof(memory) && kws_gru_pcm_alignment()==8);
    assert(INIT(memory,bytes,&PROBE,&kws_probe_model,&a)==0);
    assert(INIT(other,bytes,&PROBE,&kws_probe_model,&b)==0);
    assert(INIT(fresh,bytes,&PROBE,&kws_probe_model,&c)==0);
    kws_gru_pcm_t *invalid=(void *)1;
    assert(INIT(memory+1,bytes,&PROBE,&kws_probe_model,&invalid)==-1 && !invalid);
    assert(INIT(memory,bytes-1,&PROBE,&kws_probe_model,&invalid)==-1 && !invalid);
    kws_model_t norm=kws_probe_model;
    norm.trained=true;
    assert(INIT(memory,bytes,&PROBE,&norm,&invalid)==-1 && !invalid);
    norm.trained=false;norm.layers=(const void *)1;
    assert(INIT(memory,bytes,&PROBE,&norm,&invalid)==-1 && !invalid);
    norm.layers=NULL;norm.mean_q8=NULL;
    assert(INIT(memory,bytes,&PROBE,&norm,&invalid)==-1 && !invalid);
    assert(INIT(memory,bytes,&PROBE,&kws_probe_model,NULL)==-1);
    assert(INIT(memory,bytes,NULL,&kws_probe_model,&invalid)==-1 && !invalid);
    assert(INIT(memory,bytes,&PROBE,NULL,&invalid)==-1 && !invalid);

    int16_t previous[256]={0},pcm[512],hidden[KWS_GRU_PCM_HIDDEN],other_hidden[KWS_GRU_PCM_HIDDEN],logits[2],other_logits[2];
    kiss_fft_cpx fft_in[512],fft_out[512];uint32_t power[257];
    kws_frontend_buffers_t frontend={previous,fft_in,fft_out,power};
    kws_event_t event; kws_trace_t actual,reference;
    uint32_t random=476;
    for(unsigned frame=0;frame<256;++frame) {
        for(unsigned i=0;i<512;++i) {
            random=random*1664525u+1013904223u;
            pcm[i]=frame==0?0:frame==1?32767:frame==2?-32768:(int16_t)(random>>16);
        }
        for(unsigned half=0;half<2;++half) {
            kws_frontend_buffers_step(&frontend,&kws_probe_model,pcm+half*256,reference.input,&reference);
            uint64_t end=(uint64_t)(frame*2+half+1)*256;
            assert(kws_gru_pcm_step(a,pcm+half*256,end,&event,&actual)==0);
            assert(!event.detected && event.sample_end==end);
            assert(!memcmp(actual.logmel,reference.logmel,sizeof(actual.logmel)));
            assert(!memcmp(actual.input,reference.input,sizeof(actual.input)));
        }
        assert(kws_gru_pcm_feed(b,pcm,(uint64_t)(frame+1)*512,&event)==0 && !event.detected);
        kws_gru_pcm_values(a,hidden,logits);kws_gru_pcm_values(b,other_hidden,other_logits);
        assert(!memcmp(hidden,other_hidden,sizeof(hidden)) && !memcmp(logits,other_logits,sizeof(logits)));
    }
    memcpy(other,memory,bytes);
    assert(kws_gru_pcm_step(a,pcm,255,&event,NULL)==-1);
    assert(kws_gru_pcm_step(a,pcm,257,&event,NULL)==-1);
    assert(kws_gru_pcm_step(a,NULL,256,&event,NULL)==-1);
    assert(kws_gru_pcm_step(a,pcm,256,NULL,NULL)==-1);
    assert(kws_gru_pcm_step(NULL,pcm,256,&event,NULL)==-1);
    assert(kws_gru_pcm_feed(a,pcm,256,&event)==-1);
    assert(kws_gru_pcm_feed(a,NULL,512,&event)==-1);
    assert(!memcmp(other,memory,bytes));
    /* A skipped or backwards clock resets both the frontend and hidden state. */
    assert(kws_gru_pcm_step(a,pcm,256,&event,&actual)==0);
    assert(kws_gru_pcm_step(c,pcm,256,&event,&reference)==0);
    assert(!memcmp(actual.logmel,reference.logmel,sizeof(actual.logmel)));
    assert(!memcmp(actual.input,reference.input,sizeof(actual.input)));
    kws_gru_pcm_values(a,hidden,logits);kws_gru_pcm_values(c,other_hidden,other_logits);
    assert(!memcmp(hidden,other_hidden,sizeof(hidden)) && !memcmp(logits,other_logits,sizeof(logits)));
    assert(INIT(fresh,bytes,&PROBE,&kws_probe_model,&c)==0);
    assert(kws_gru_pcm_step(a,pcm,1280,&event,&actual)==0);
    assert(kws_gru_pcm_step(c,pcm,1280,&event,&reference)==0);
    kws_gru_pcm_values(a,hidden,logits);kws_gru_pcm_values(c,other_hidden,other_logits);
    assert(!memcmp(hidden,other_hidden,sizeof(hidden)) && !memcmp(logits,other_logits,sizeof(logits)));
    /* Even extreme positive logits cannot make the resource adapter wake. */
    probe_model_t positive=PROBE;
    positive.output_bias[0]=-32768;positive.output_bias[1]=32767;
    assert(INIT(fresh,bytes,&positive,&kws_probe_model,&c)==0);
    assert(kws_gru_pcm_step(c,pcm,256,&event,NULL)==0);
    assert(event.score_q8==32767 && !event.detected);
#ifdef AGENT_KWS_GRU64_RESOURCE_ONLY
    memcpy(other,fresh,bytes);
    positive.abi=0;
    assert(INIT(fresh,bytes,&positive,&kws_probe_model,&invalid)==-1 && !invalid);
    assert(!memcmp(other,fresh,bytes));
#endif

#ifdef PROBE_HEAP_WRAPPERS
    assert(heap_calls==0);
#endif
    printf("frontend parity512 frames; feed/step parity256 blocks; reset/gap/invalid/always-false passed; heap_calls=0; workspace=%zu\n",bytes);
    return 0;
}
