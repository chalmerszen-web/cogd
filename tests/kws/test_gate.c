#include "kws.h"
#include "kws_fusion.h"
#include "kws_internal.h"
#include <assert.h>
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

static void decisions(void)
{
    _Static_assert(sizeof(kws_detector_t)==24,"gate must not add state");
    for(unsigned stable=0;stable<2;++stable) {
        kws_detector_t d; kws_detector_reset(&d,268);
        kws_detector_stability(&d,stable!=0);
        uint64_t end=0;
        for(unsigned i=0;i<64;++i) { end+=512; assert(!kws_detector_step_armed(&d,-512,end,false)); }
        for(unsigned i=0;i<16;++i) {
            end+=512; assert(!kws_detector_step_armed(&d,512,end,false));
            assert(!d.votes && !d.cooldown_until);
        }
        /* Inference warmup is preserved, but guard votes do not leak. */
        unsigned needed=stable?4:2;
        for(unsigned i=1;i<=needed;++i) {
            end+=512; assert(kws_detector_step_armed(&d,512,end,true)==(i==needed));
        }
        uint64_t real_cooldown=d.cooldown_until;
        for(unsigned i=0;i<10;++i) {
            end+=512; assert(!kws_detector_step_armed(&d,512,end,false));
            assert(d.cooldown_until==real_cooldown && !d.votes);
        }
        while(end+512<real_cooldown) { end+=512; assert(!kws_detector_step_armed(&d,512,end,true)); }
        end+=512; assert(kws_detector_step_armed(&d,512,end,true));
        /* A sample gap retains policy, discards all history and warms again. */
        end+=4096; assert(!kws_detector_step_armed(&d,512,end,false));
        assert(d.stable==(stable!=0) && d.threshold_q8==268 && d.blocks==1 && !d.cooldown_until);
        for(unsigned i=1;i<64;++i) { end+=512; assert(!kws_detector_step_armed(&d,512,end,false)); }
        for(unsigned i=1;i<=needed;++i) {
            end+=512; assert(kws_detector_step_armed(&d,268,end,true)==(i==needed));
        }
        /* The original ignored-hit timeline now admits the genuine run. */
        kws_detector_reset(&d,268); kws_detector_stability(&d,stable!=0); end=0;
        for(unsigned i=0;i<64;++i) { end+=512; assert(!kws_detector_step_armed(&d,-512,end,true)); }
        for(unsigned i=1;i<=16;++i) { end+=512; assert(!kws_detector_step_armed(&d,512,end,false)); }
        for(unsigned i=17;i<=28;++i) { end+=512; assert(!kws_detector_step_armed(&d,-512,end,true)); }
        unsigned accepted=0;
        for(unsigned i=29;i<=32;++i) { end+=512; accepted+=kws_detector_step_armed(&d,512,end,true); }
        assert(accepted==1);
    }
}

static void history(void)
{
    size_t bytes=kws_fusion_size(),single_bytes=kws_workspace_size();
    assert(bytes==sizeof(kws_fusion_t) && bytes<=12632-1024);
    kws_fusion_t *a=NULL,*b=NULL;
    kws_handle_t *c=NULL,*d=NULL;
    void *ma=malloc(bytes),*mb=malloc(bytes),*copy=malloc(bytes);
    void *mc=malloc(single_bytes),*md=malloc(single_bytes),*single_copy=malloc(single_bytes);
    assert(ma && mb && copy && mc && md && single_copy);
    assert(!kws_fusion_init(ma,bytes,&kws_trained_model,&kws_secondary_model,&a));
    assert(!kws_fusion_init(mb,bytes,&kws_trained_model,&kws_secondary_model,&b));
    uint64_t base=kws_fusion_prime(a); assert(base==32768 && kws_fusion_prime(b)==base);
    assert(!memcmp(a,b,bytes));
    kws_fusion_threshold(a,INT16_MIN); kws_fusion_threshold(b,INT16_MIN);
    assert(!kws_init(mc,single_bytes,&kws_trained_model,&c));
    assert(!kws_init(md,single_bytes,&kws_trained_model,&d));
    kws_set_threshold(c,INT16_MIN); kws_set_threshold(d,INT16_MIN);
    no_allocation=true;
    int16_t pcm[512]; uint32_t random=20261001;
    for(unsigned block=0;block<256;++block) {
        for(unsigned i=0;i<512;++i) {
            random=random*1664525u+1013904223u;
            pcm[i]=block==0?0:block==1?32767:block==2?-32768:(int16_t)(random>>16);
        }
        bool armed=block%48>=16;
        uint64_t end=base+(block+1)*512;
        if(block==128) base+=4096,end+=4096;
        kws_event_t ea,eb,ec,ed;
        assert(!kws_fusion_feed_512(a,pcm,end,&ea));
        assert(!kws_fusion_feed_512_armed(b,pcm,end,armed,&eb));
        assert(ea.score_q8==eb.score_q8 && ea.sample_end==eb.sample_end);
        assert(armed || !eb.detected);
        memcpy(copy,a,bytes); ((kws_fusion_t *)copy)->detector=b->detector;
        assert(!memcmp(copy,b,bytes)); /* All frontend, NN and smoothing bytes. */
        assert(!kws_feed_512(c,pcm,end,&ec));
        assert(!kws_feed_512_armed(d,pcm,end,armed,&ed));
        assert(ec.score_q8==ed.score_q8 && ec.sample_end==ed.sample_end);
        assert(armed || !ed.detected);
        memcpy(single_copy,c,single_bytes); ((kws_handle_t *)single_copy)->detector=d->detector;
        assert(!memcmp(single_copy,d,single_bytes));
    }
    memcpy(copy,b,bytes); kws_event_t e;
    assert(kws_fusion_feed_512_armed(NULL,pcm,512,false,&e)<0);
    assert(kws_fusion_feed_512_armed(b,NULL,512,false,&e)<0);
    assert(kws_fusion_feed_512_armed(b,pcm,511,false,&e)<0);
    assert(kws_fusion_feed_512_armed(b,pcm,512,false,NULL)<0);
    assert(!memcmp(copy,b,bytes));
    memcpy(single_copy,d,single_bytes);
    assert(kws_feed_512_armed(NULL,pcm,512,false,&e)<0);
    assert(kws_feed_512_armed(d,NULL,512,false,&e)<0);
    assert(kws_feed_512_armed(d,pcm,511,false,&e)<0);
    assert(kws_feed_512_armed(d,pcm,512,false,NULL)<0);
    assert(!memcmp(single_copy,d,single_bytes));
    no_allocation=false;
    free(ma);free(mb);free(copy);free(mc);free(md);free(single_copy);
    printf("256 fusion/single guarded PCM blocks: all acoustic/state/score bytes identical; fusion=%zu,detector=%zu; no allocation\n",bytes,sizeof(kws_detector_t));
}

int main(void)
{ decisions(); history(); puts("guard votes, ghost cooldown, accepted cooldown, warmup, gaps and invalid inputs passed"); }
