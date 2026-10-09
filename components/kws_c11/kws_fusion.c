#include "kws_fusion.h"
#include "kws_internal.h"
#include <stdalign.h>
#include <string.h>

#ifdef AGENT_KWS_EL_SILENCE_PRIME
#include "generated/silence_el.inc"
#endif
size_t kws_fusion_size(void) { return sizeof(kws_fusion_t); }
size_t kws_fusion_alignment(void) { return alignof(kws_fusion_t); }
uint64_t kws_fusion_prime(kws_fusion_t *s)
{
#ifdef AGENT_KWS_EL_SILENCE_PRIME
    /* The build verifies exact model/frontend/algorithm hashes. Pointer
     * identity also rejects another pair with superficially similar metadata.
     * Only a fresh default-threshold init may receive this initial state. */
    if(!s || s->first.model!=&kws_trained_model || s->second_model!=&kws_secondary_model ||
       s->last_sample || s->detector.blocks || s->detector.threshold_q8 || s->detector.stable || s->half || s->count) return 0;
    memcpy(s->first.previous,kws_silence_el.previous,sizeof(s->first.previous));
    s->first.neural=kws_silence_el.first;s->second=kws_silence_el.second;
    s->detector=kws_silence_el.detector;s->last_sample=kws_silence_el.last_sample;
    memcpy(s->recent,kws_silence_el.recent,sizeof(s->recent));
    memcpy(s->scores,kws_silence_el.scores,sizeof(s->scores));s->score=kws_silence_el.score;
    s->next=kws_silence_el.next;s->count=kws_silence_el.count;s->half=kws_silence_el.half;
    return s->last_sample;
#else
    (void)s;return 0;
#endif
}
void kws_fusion_reset(kws_fusion_t *s)
{
    const kws_model_t *first=s->first.model,*second=s->second_model;
    int16_t threshold=s->detector.threshold_q8;
    bool stable=s->detector.stable;
    memset(s,0,sizeof(*s));s->first.model=first;s->second_model=second;
    kws_detector_reset(&s->detector,threshold);
    kws_detector_stability(&s->detector,stable);
}
int kws_fusion_init(void *memory,size_t bytes,const kws_model_t *first,const kws_model_t *second,kws_fusion_t **out)
{
    if(!out) return -1;
    *out=NULL;
    if(!memory || bytes<sizeof(kws_fusion_t) || (uintptr_t)memory%alignof(kws_fusion_t) ||
       !kws_model_valid(first) || !kws_model_valid(second) ||
       memcmp(first->mean_q8,second->mean_q8,40*sizeof(int16_t)) ||
       memcmp(first->inverse_std_q12,second->inverse_std_q12,40*sizeof(uint16_t))) return -1;
    kws_fusion_t *s=memory;memset(s,0,sizeof(*s));
    s->first.model=first;s->second_model=second;*out=s;return 0;
}
void kws_fusion_threshold(kws_fusion_t *s,int16_t threshold)
{ s->detector.threshold_q8=threshold; }
void kws_fusion_stability(kws_fusion_t *s,bool stable)
{ kws_detector_stability(&s->detector,stable); }
void kws_fusion_scores(const kws_fusion_t *s,int16_t scores[2])
{ memcpy(scores,s->scores,sizeof(s->scores)); }
int kws_fusion_step_pcm_armed(kws_fusion_t *s,const int16_t pcm[256],uint64_t end,bool armed,kws_event_t *event,kws_trace_t *trace)
{
    if(!s || !pcm || !event || end<256) return -1;
    if(s->last_sample && end-s->last_sample!=256) kws_fusion_reset(s);
    s->last_sample=end;
    kws_trace_t *t=trace?trace:&s->first.trace;
    kws_frontend(&s->first,pcm,t);
    s->scores[0]=kws_neural_step(&s->first.neural,s->first.model,t->input,t);
    s->scores[1]=kws_neural_step(&s->second,s->second_model,t->input,NULL);
    bool detected=false;
    if(++s->half==2) {
        s->half=0;
        s->recent[s->next]=(int16_t)(((int32_t)s->scores[0]+s->scores[1])/2);
        s->next=(uint8_t)((s->next+1)%3);
        if(s->count<3) ++s->count;
        int32_t sum=0;
        for(unsigned i=0;i<s->count;++i) sum+=s->recent[i];
        s->score=(int16_t)(sum/s->count);
        detected=kws_detector_step_armed(&s->detector,s->score,end,armed);
    }
    *event=(kws_event_t){s->score,detected && s->first.model->trained && s->second_model->trained,end};
    return 0;
}
int kws_fusion_step_pcm(kws_fusion_t *s,const int16_t pcm[256],uint64_t end,kws_event_t *event,kws_trace_t *trace)
{ return kws_fusion_step_pcm_armed(s,pcm,end,true,event,trace); }
int kws_fusion_feed_512_armed(kws_fusion_t *s,const int16_t pcm[512],uint64_t end,bool armed,kws_event_t *event)
{
    if(!s || !pcm || !event || end<512) return -1;
    int error=kws_fusion_step_pcm_armed(s,pcm,end-256,armed,event,NULL);
    return error?error:kws_fusion_step_pcm_armed(s,pcm+256,end,armed,event,NULL);
}
int kws_fusion_feed_512(kws_fusion_t *s,const int16_t pcm[512],uint64_t end,kws_event_t *event)
{ return kws_fusion_feed_512_armed(s,pcm,end,true,event); }
