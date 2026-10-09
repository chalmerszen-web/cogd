#include "kws_fusion.h"
#include "kws_internal.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void same(const kws_fusion_t *a,const kws_fusion_t *b)
{
    assert(!memcmp(a->first.previous,b->first.previous,sizeof(a->first.previous)));
    assert(!memcmp(&a->first.neural,&b->first.neural,sizeof(a->first.neural)));
    assert(!memcmp(&a->second,&b->second,sizeof(a->second)));
    assert(a->detector.last_sample==b->detector.last_sample && a->detector.cooldown_until==b->detector.cooldown_until);
    assert(a->detector.blocks==b->detector.blocks && a->detector.votes==b->detector.votes && a->detector.threshold_q8==b->detector.threshold_q8 && a->detector.stable==b->detector.stable);
    assert(a->last_sample==b->last_sample && !memcmp(a->recent,b->recent,sizeof(a->recent)) && !memcmp(a->scores,b->scores,sizeof(a->scores)));
    assert(a->score==b->score && a->next==b->next && a->count==b->count && a->half==b->half);
}
static void event_same(const kws_event_t *a,const kws_event_t *b)
{ assert(a->score_q8==b->score_q8 && a->detected==b->detected && a->sample_end==b->sample_end); }
static void run(int16_t threshold,unsigned variant,bool stable)
{
    void *left=malloc(kws_fusion_size()),*right=malloc(kws_fusion_size());assert(left && right);
    kws_fusion_t *reference,*primed;
    assert(!kws_fusion_init(left,kws_fusion_size(),&kws_trained_model,&kws_secondary_model,&reference));
    assert(!kws_fusion_init(right,kws_fusion_size(),&kws_trained_model,&kws_secondary_model,&primed));
    kws_event_t a,b;int16_t pcm[512]={0};uint64_t clock=0;
    for(unsigned i=0;i<128;++i) {
        clock+=256;assert(!kws_fusion_step_pcm(reference,pcm,clock,&a,NULL));assert(!a.detected);
    }
    assert(kws_fusion_prime(primed)==clock && clock==32768);
    assert(kws_fusion_prime(primed)==0); /* Never overwrite an already-running model. */
    same(reference,primed);
    kws_fusion_stability(reference,stable);kws_fusion_stability(primed,stable);
    kws_fusion_threshold(reference,threshold);kws_fusion_threshold(primed,threshold);
    uint32_t random=71+variant;unsigned events=0;
    for(unsigned frame=0;frame<512;++frame) {
        unsigned kind=(frame/17+variant)%5;
        for(unsigned i=0;i<512;++i) {
            random=random*UINT32_C(1664525)+UINT32_C(1013904223);
            pcm[i]=kind==0?0:kind==1?INT16_MAX:kind==2?INT16_MIN:kind==3?(int16_t)(random>>16):(int16_t)((int)(random%1001)-500);
        }
        /* Alternate two explicit 16-ms steps and a single 32-ms wrapper.
         * Each 16-ms trace is independently checked, including zero stretches. */
        kws_trace_t ta,tb;
        if(frame%3) {
            for(unsigned half=0;half<2;++half) {
                clock+=256;
                assert(!kws_fusion_step_pcm(reference,pcm+half*256,clock,&a,&ta));
                assert(!kws_fusion_step_pcm(primed,pcm+half*256,clock,&b,&tb));
                assert(!memcmp(&ta,&tb,sizeof(ta)));event_same(&a,&b);same(reference,primed);
                events+=b.detected;
            }
        } else {
            clock+=512;
            assert(!kws_fusion_feed_512(reference,pcm,clock,&a));
            assert(!kws_fusion_feed_512(primed,pcm,clock,&b));
            assert(!memcmp(&reference->first.trace,&primed->first.trace,sizeof(kws_trace_t)));
            event_same(&a,&b);same(reference,primed);events+=b.detected;
        }
    }
    /* Both states must retain the original discontinuity reset behavior. */
    clock+=2048;assert(!kws_fusion_feed_512(reference,pcm,clock,&a));
    assert(!kws_fusion_feed_512(primed,pcm,clock,&b));event_same(&a,&b);same(reference,primed);
    assert(!a.detected && reference->detector.blocks==1 && reference->detector.stable==stable && primed->detector.stable==stable);
    if(threshold==INT16_MIN)assert(events>0);
    printf("prime parity threshold=%d variant=%u frames=1024 events=%u OK\n",threshold,variant,events);
    free(left);free(right);
}
static void pcm_file(const char *path)
{
    FILE *file=fopen(path,"rb");assert(file);
    void *left=malloc(kws_fusion_size()),*right=malloc(kws_fusion_size());assert(left && right);
    kws_fusion_t *reference,*primed;
    assert(!kws_fusion_init(left,kws_fusion_size(),&kws_trained_model,&kws_secondary_model,&reference));
    assert(!kws_fusion_init(right,kws_fusion_size(),&kws_trained_model,&kws_secondary_model,&primed));
    int16_t pcm[256]={0};kws_event_t a,b;uint64_t clock=0;
    for(unsigned i=0;i<128;++i) {clock+=256;assert(!kws_fusion_step_pcm(reference,pcm,clock,&a,NULL));}
    assert(kws_fusion_prime(primed)==clock);same(reference,primed);
    kws_fusion_threshold(reference,625);kws_fusion_threshold(primed,625);
    unsigned frames=0,events=0;
    for(;;) {
        uint8_t bytes[512]={0};size_t n=fread(bytes,1,sizeof(bytes),file);assert(n%2==0);
        if(!n)break;
        for(unsigned i=0;i<256;++i) {
            unsigned value=(unsigned)bytes[i*2] | (unsigned)bytes[i*2+1]<<8;
            pcm[i]=(int16_t)(value>=32768?(int)value-65536:(int)value);
        }
        clock+=256;kws_trace_t ta,tb;
        assert(!kws_fusion_step_pcm(reference,pcm,clock,&a,&ta));
        assert(!kws_fusion_step_pcm(primed,pcm,clock,&b,&tb));
        assert(!memcmp(&ta,&tb,sizeof(ta)));event_same(&a,&b);same(reference,primed);
        ++frames;events+=b.detected;
    }
    assert(frames && !ferror(file) && fclose(file)==0);
    printf("PCM parity %s frames=%u events=%u OK\n",path,frames,events);
    free(left);free(right);
}
static void other_models(void)
{
    void *memory=malloc(kws_fusion_size()),*saved=malloc(kws_fusion_size());assert(memory && saved);
    kws_fusion_t *s;const kws_model_t copy=kws_trained_model;
    assert(!kws_fusion_init(memory,kws_fusion_size(),&copy,&kws_secondary_model,&s));
    memcpy(saved,memory,kws_fusion_size());assert(!kws_fusion_prime(s));assert(!memcmp(saved,memory,kws_fusion_size()));
    assert(!kws_fusion_init(memory,kws_fusion_size(),&kws_trained_model,&copy,&s));
    memcpy(saved,memory,kws_fusion_size());assert(!kws_fusion_prime(s));assert(!memcmp(saved,memory,kws_fusion_size()));
    assert(!kws_fusion_init(memory,kws_fusion_size(),&kws_trained_model,&kws_secondary_model,&s));
    kws_fusion_threshold(s,625);memcpy(saved,memory,kws_fusion_size());
    assert(!kws_fusion_prime(s));assert(!memcmp(saved,memory,kws_fusion_size()));
    assert(!kws_fusion_init(memory,kws_fusion_size(),&kws_trained_model,&kws_secondary_model,&s));
    kws_fusion_stability(s,true);memcpy(saved,memory,kws_fusion_size());
    assert(!kws_fusion_prime(s));assert(!memcmp(saved,memory,kws_fusion_size()));
    assert(!kws_fusion_prime(NULL));free(memory);free(saved);
}
int main(int argc,char **argv)
{
    const int16_t thresholds[]={INT16_MIN,-512,0,625,INT16_MAX};
    for(unsigned mode=0;mode<2;++mode)
        for(unsigned i=0;i<sizeof(thresholds)/sizeof(*thresholds);++i)run(thresholds[i],i,mode!=0);
    other_models();
    for(int i=1;i<argc;++i)pcm_file(argv[i]);
    puts("seed copies causal fields only; no new runtime allocation; other models unchanged");return 0;
}
