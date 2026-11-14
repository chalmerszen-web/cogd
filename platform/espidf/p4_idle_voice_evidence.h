#ifndef P4_IDLE_VOICE_EVIDENCE_H
#define P4_IDLE_VOICE_EVIDENCE_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    P4_IDLE_VOICE_UNKNOWN,
    P4_IDLE_VOICE_INCONCLUSIVE,
    P4_IDLE_VOICE_ROUGH_UNSTRUCTURED,
} p4_idle_voice_evidence_t;

/* A structure observation carries no input authority. Use its absence only
 * with uninterrupted calibrated, DAC-quiet producer coverage. */
enum { P4_IDLE_VOICE_FRAME_VALID=1u, P4_IDLE_VOICE_FRAME_STRUCTURE=2u,
       P4_IDLE_VOICE_FRAME_ELIGIBLE=4u, P4_IDLE_VOICE_FRAME_ROUGH=8u };
typedef struct {
    uint32_t from_ms, through_ms, previous_ms;
    unsigned eligible, rough, weighted_total, weighted_rough;
    bool started, invalid, structured;
} p4_idle_voice_window_t;

/* Normalize excess against the same per-frame adaptive gate. Cap each
 * contribution at six threshold units so an impulse cannot own a window. */
static inline unsigned p4_idle_voice_energy_weight(uint32_t energy,uint32_t threshold)
{
    if(!threshold || energy<=threshold)return 0;
    uint64_t excess=(uint64_t)(energy-threshold)*32u;
    if(excess>(uint64_t)threshold*192u)return 192u;
    return (unsigned)(excess/threshold);
}

/* Gain-independent first-difference energy relative to centered variance.
 * Use exact PCM, including negative samples, without floating point. */
static inline bool p4_idle_voice_pcm_rough(const int16_t pcm[512])
{
    int32_t sum=0;
    uint64_t square=0,difference=0;
    for(unsigned i=0;i<512;++i){
        int32_t value=pcm[i];
        sum+=value;square+=(int64_t)value*value;
        if(i){int32_t delta=value-pcm[i-1];difference+=(int64_t)delta*delta;}
    }
    uint64_t variance=512u*square-(int64_t)sum*sum;
    return variance && difference*2560u>variance;
}

/* Scan producer frames newest to oldest. False means older rows are outside
 * coverage. Millisecond distances tolerate audio task scheduling jitter. */
static inline bool p4_idle_voice_frame_weighted(p4_idle_voice_window_t *s,uint32_t at,unsigned flags,unsigned weight)
{
    if((int32_t)(at-s->through_ms)>0)return true;
    if((int32_t)(at-s->from_ms)<0)return false;
    if(!s->started){
        s->started=true;
        if(s->through_ms-at>52u)s->invalid=true;
    } else {
        uint32_t gap=s->previous_ms-at;
        if(gap<16u || gap>52u)s->invalid=true;
    }
    s->previous_ms=at;
    if(!(flags&P4_IDLE_VOICE_FRAME_VALID))s->invalid=true;
    if(flags&P4_IDLE_VOICE_FRAME_STRUCTURE)s->structured=true;
    if(flags&P4_IDLE_VOICE_FRAME_ELIGIBLE){
        ++s->eligible;
        s->weighted_total+=weight;
        if(flags&P4_IDLE_VOICE_FRAME_ROUGH){++s->rough;s->weighted_rough+=weight;}
    }
    return true;
}

static inline bool p4_idle_voice_frame(p4_idle_voice_window_t *s,uint32_t at,unsigned flags)
{ return p4_idle_voice_frame_weighted(s,at,flags,0); }

static inline p4_idle_voice_evidence_t p4_idle_voice_finish(const p4_idle_voice_window_t *s)
{
    if(!s->started || s->invalid || s->previous_ms-s->from_ms>52u)return P4_IDLE_VOICE_UNKNOWN;
    bool dominated=s->rough*5u>=s->eligible*4u ||
        (s->rough>=6u && s->rough*2u>s->eligible && s->weighted_total &&
         s->weighted_rough*5u>=s->weighted_total*4u);
    return !s->structured && s->eligible>=6u && dominated?
        P4_IDLE_VOICE_ROUGH_UNSTRUCTURED:P4_IDLE_VOICE_INCONCLUSIVE;
}
#endif
