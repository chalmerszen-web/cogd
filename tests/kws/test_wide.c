#include "kws_internal.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool no_allocation;
void *__real_malloc(size_t n);
void *__real_calloc(size_t n,size_t m);
void *__real_realloc(void *p,size_t n);
void __real_free(void *p);
void *__wrap_malloc(size_t n) { assert(!no_allocation); return __real_malloc(n); }
void *__wrap_calloc(size_t n,size_t m) { assert(!no_allocation); return __real_calloc(n,m); }
void *__wrap_realloc(void *p,size_t n) { assert(!no_allocation); return __real_realloc(p,n); }
void __wrap_free(void *p) { assert(!no_allocation); __real_free(p); }

static void invalid_models(void *memory,size_t bytes)
{
    kws_layer_t layers[12];
    memcpy(layers,kws_probe_model.layers,sizeof(layers));
    kws_model_t wrong=kws_probe_model;wrong.layers=layers;
    kws_handle_t *handle=(kws_handle_t *)memory;
    layers[0].outputs=24;
    assert(kws_init(memory,bytes,&wrong,&handle)<0 && !handle);
    layers[0]=kws_probe_model.layers[0];layers[1].inputs=24;
    assert(kws_init(memory,bytes,&wrong,&handle)<0 && !handle);
    layers[1]=kws_probe_model.layers[1];layers[9].dilation=8;
    assert(kws_init(memory,bytes,&wrong,&handle)<0 && !handle);
    layers[9]=kws_probe_model.layers[9];
    int32_t bias[48]={0};bias[47]=INT32_MAX;layers[2].bias=bias;
    assert(kws_init(memory,bytes,&wrong,&handle)<0 && !handle);
    bias[47]=INT32_MIN;
    assert(kws_init(memory,bytes,&wrong,&handle)<0 && !handle);
}

int main(void)
{
    _Static_assert(KWS_CHANNELS==48 && KWS_TRACE_VALUES==529,"48 trace ABI");
    _Static_assert(sizeof(((kws_neural_t *)0)->history)==6032,"48 causal history");
    assert(kws_channels()==48 && kws_trace_values()==529);
    assert(!kws_probe_model.trained); /* A resource probe must never wake. */
    size_t bytes=kws_workspace_size();
    assert(bytes<=16384 && kws_workspace_alignment()>=8);
    unsigned char *allocation=malloc(bytes+128);
    assert(allocation);memset(allocation,0xa5,bytes+128);
    void *memory=allocation+64;kws_handle_t *handle=NULL;
    assert(kws_init((char *)memory+4,bytes,&kws_probe_model,&handle)<0 && !handle);
    assert(kws_init(memory,bytes-1,&kws_probe_model,&handle)<0 && !handle);
    invalid_models(memory,bytes);
    assert(!kws_init(memory,bytes,&kws_probe_model,&handle));
    int16_t pcm[512]={0};uint32_t random=13;kws_event_t event;
    no_allocation=true;
    assert(kws_feed_512(handle,pcm,0,&event)<0);
    for(unsigned block=0;block<1024;++block) {
        for(unsigned i=0;i<512;++i) {
            random=random*1664525u+1013904223u;
            unsigned raw=random>>16;
            pcm[i]=block==0?0:block==1?INT16_MAX:block==2?INT16_MIN:
                (int16_t)(raw<=INT16_MAX?(int)raw:(int)raw-65536);
        }
        assert(!kws_feed_512_armed(handle,pcm,(uint64_t)(block+1)*512,block>=16,&event));
        assert(!event.detected);
    }
    kws_trace_t first,second;
    kws_reset(handle);kws_step_pcm(handle,pcm,&first);
    kws_reset(handle);kws_step_pcm(handle,pcm,&second);
    assert(!memcmp(&first,&second,sizeof(first)));
    kws_set_threshold(handle,268);kws_set_stability(handle,true);
    assert(!kws_feed_512_armed(handle,pcm,2000000,false,&event));
    assert(!event.detected && handle->detector.threshold_q8==268 && handle->detector.stable);
    for(unsigned i=0;i<64;++i) {
        assert(allocation[i]==0xa5 && allocation[64+bytes+i]==0xa5);
    }
    no_allocation=false;free(allocation);
    printf("48 channels; workspace=%zu neural=%zu trace=%zu; invalid shapes/overflow,1024 blocks, guards and no allocation passed\n",
           bytes,sizeof(kws_neural_t),sizeof(kws_trace_t));
    return 0;
}
