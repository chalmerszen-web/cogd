#include "kws.h"
#include "kws_fusion.h"
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

static void stability(void)
{
    _Static_assert(sizeof(kws_detector_t)==24,"detector must retain its RAM budget");
    kws_detector_t d;uint64_t end=0;
    kws_detector_reset(&d,256);kws_detector_stability(&d,true);
    for(unsigned i=0;i<64;++i) {end+=512;assert(!kws_detector_step(&d,0,end));}
    for(unsigned pulse=1;pulse<=3;++pulse) {
        for(unsigned i=0;i<pulse;++i) {end+=512;assert(!kws_detector_step(&d,512,end));}
        end+=512;assert(!kws_detector_step(&d,0,end));
    }
    /* Separate high blocks must not combine across a low block. */
    const int16_t broken[]={512,512,0,512,512,0};
    for(unsigned i=0;i<6;++i) {end+=512;assert(!kws_detector_step(&d,broken[i],end));}
    for(unsigned i=0;i<3;++i) {end+=512;assert(!kws_detector_step(&d,512,end));}
    end+=512;assert(kws_detector_step(&d,256,end)); /* Inclusive threshold. */
    for(unsigned i=0;i<46;++i) {end+=512;assert(!kws_detector_step(&d,512,end));}
    end+=512;assert(kws_detector_step(&d,512,end));
    /* A discontinuity keeps the policy, but discards votes and warms again. */
    end+=4096;assert(!kws_detector_step(&d,512,end));assert(d.stable && d.blocks==1);
    for(unsigned i=1;i<64;++i) {end+=512;assert(!kws_detector_step(&d,0,end));}
    for(unsigned i=0;i<3;++i) {end+=512;assert(!kws_detector_step(&d,512,end));}
    end+=512;assert(kws_detector_step(&d,512,end));
    kws_detector_reset(&d,256);assert(!d.stable);
    end=0;for(unsigned i=0;i<64;++i) {end+=512;assert(!kws_detector_step(&d,0,end));}
    end+=512;assert(!kws_detector_step(&d,512,end));
    end+=512;assert(!kws_detector_step(&d,0,end));
    end+=512;assert(kws_detector_step(&d,512,end)); /* Legacy two-of-three. */
}

int main(void)
{
    stability();
    assert(kws_requantize(-3,1,-128,127)==-2);
    assert(kws_requantize(3,1,-128,127)==2);
    assert(kws_requantize(INT_MIN,32,-128,127)==-1);
    assert(kws_requantize(INT_MIN,33,-128,127)==0);
    assert(kws_requantize(INT_MAX,-32,-128,127)==127);
    assert(kws_requantize(INT_MIN,INT_MIN,-128,127)==-128);
    assert(kws_requantize(0,INT_MIN,-128,127)==0);
    assert(kws_requantize(INT_MIN,-31,INT_MIN,INT_MAX)==INT_MIN);
    kws_detector_t d;
    kws_detector_reset(&d,256);
    for(unsigned i=1;i<64;++i) assert(!kws_detector_step(&d,512,i*512));
    assert(kws_detector_step(&d,512,64*512));
    for(unsigned i=65;i<111;++i) assert(!kws_detector_step(&d,512,i*512));
    assert(kws_detector_step(&d,512,111*512));
    assert(!kws_detector_step(&d,512,200*512));
    size_t size=kws_workspace_size();
    assert(size<=16384);
    void *memory=malloc(size);
    kws_handle_t *k=NULL;
    assert(kws_workspace_alignment()>=8);
    assert(kws_init((char *)memory+4,size,&kws_probe_model,&k)<0);
    assert(kws_init(memory,size-1,&kws_probe_model,&k)<0);
    assert(!kws_init(memory,size,&kws_probe_model,&k));
    int16_t pcm[512]; uint32_t random=7;
    no_allocation=true;
    for(unsigned block=0;block<4096;++block) {
        for(unsigned j=0;j<512;++j) {
            random=random*1664525u+1013904223u;
            pcm[j]=block==0?0:block==1?32767:block==2?-32768:(int16_t)(random>>16);
        }
        kws_event_t event;
        assert(!kws_feed_512(k,pcm,(uint64_t)(block+1)*512,&event));
        assert(!event.detected); /* A probe can never masquerade as trained KWS. */
    }
    kws_trace_t first,second;
    kws_reset(k); kws_step_pcm(k,pcm,&first);
    kws_reset(k); kws_step_pcm(k,pcm,&second);
    assert(!memcmp(&first,&second,sizeof(first)));
    no_allocation=false; free(memory);
    size_t fusion_size=kws_fusion_size();assert(fusion_size<=20*1024);
    void *fusion_memory=malloc(fusion_size),*single_memory=malloc(size);
    kws_fusion_t *fusion=NULL;kws_model_t trained=kws_probe_model;trained.trained=true;
    assert(kws_fusion_alignment()>=8);
    assert(kws_fusion_init(fusion_memory,fusion_size-1,&trained,&trained,&fusion)<0);
    assert(kws_fusion_init((char *)fusion_memory+4,fusion_size,&trained,&trained,&fusion)<0);
    int16_t wrong_mean[40];memcpy(wrong_mean,trained.mean_q8,sizeof(wrong_mean));++wrong_mean[0];
    kws_model_t wrong=trained;wrong.mean_q8=wrong_mean;
    assert(kws_fusion_init(fusion_memory,fusion_size,&trained,&wrong,&fusion)<0);
    assert(!kws_fusion_init(fusion_memory,fusion_size,&trained,&trained,&fusion));
    assert(!kws_init(single_memory,size,&trained,&k));
    kws_fusion_threshold(fusion,INT16_MIN);kws_detector_reset(&d,INT16_MIN);
    int16_t history[3]={0};unsigned count=0,next=0;
    no_allocation=true;
    for(unsigned frame=0;frame<512;++frame) {
        for(unsigned j=0;j<256;++j) {random=random*1664525u+1013904223u;pcm[j]=(int16_t)(random>>16);}
        kws_step_pcm(k,pcm,&first);kws_event_t event;
        assert(!kws_fusion_step_pcm(fusion,pcm,(frame+1)*256,&event,&second));
        assert(!memcmp(&first,&second,sizeof(first)));
        int16_t scores[2];kws_fusion_scores(fusion,scores);
        assert(scores[0]==first.layer[264] && scores[1]==scores[0]);
        if(frame&1) {
            history[next]=scores[0];next=(next+1)%3;if(count<3) ++count;
            int32_t sum=0;for(unsigned j=0;j<count;++j) sum+=history[j];
            int16_t expected=(int16_t)(sum/(int32_t)count);
            assert(event.score_q8==expected);
            assert(event.detected==kws_detector_step(&d,expected,(frame+1)*256));
        } else assert(!event.detected);
    }
    kws_event_t gap;assert(!kws_fusion_feed_512(fusion,pcm,200000,&gap));assert(!gap.detected);
    kws_fusion_reset(fusion);assert(!kws_fusion_feed_512(fusion,pcm,512,&gap));assert(!gap.detected);
    no_allocation=false;free(single_memory);free(fusion_memory);
    printf("workspace=%zu; quantization, detector, reset, 4096 adversarial blocks passed\n",size);
    printf("fusion_workspace=%zu; shared frontend, independent neural state, smoothing, reset and no allocation passed\n",fusion_size);
    return 0;
}
