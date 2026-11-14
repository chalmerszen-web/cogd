#ifndef P4_NEAR_ONSET_H
#define P4_NEAR_ONSET_H
#include <stdbool.h>
#include <stdint.h>

/* This tracks only the pre-roll boundary. It cannot confirm speech or stop
 * playback; the existing VAD, echo rejection and strong votes still decide.
 * Residual-echo suppression can hide about one second inside a recorded
 * near-end phrase. Keep that onset for replay; this is not a VAD endpoint. */
typedef struct {uint32_t first,last;bool active;} p4_near_onset_t;
static inline void p4_near_onset_observe(p4_near_onset_t *s,uint32_t now,bool eligible)
{
    if(!eligible)return;
    if(!s->active || (uint32_t)(now-s->last)>1200){s->first=now;s->active=true;}
    s->last=now;
}

/* An independently voiced linear frame may survive while NLP hides the
 * opening syllable. Keep a separate replay hint: it contributes no vote
 * and cannot stop playback. The recorded prefix loss had 1215 ms between
 * hints, so the strict 1200 ms onset history discarded it. Bound both the
 * gap and total age; never rewind indefinitely through the previous reply. */
static inline void p4_near_onset_hint(p4_near_onset_t *s,uint32_t now,bool voiced)
{
    if(!voiced)return;
    if(!s->active || (uint32_t)(now-s->last)>2000){s->first=now;s->active=true;}
    s->last=now;
}
static inline uint32_t p4_near_onset_select(const p4_near_onset_t *strict,
    const p4_near_onset_t *hint,uint32_t now)
{
    if(hint->active && (uint32_t)(now-hint->last)<=2000 &&
       (uint32_t)(now-hint->first)<=3000 &&
       (!strict->active || (int32_t)(hint->first-strict->first)<0))return hint->first;
    return strict->first;
}

/* In reference-gap confirmation the first visible near evidence can arrive
 * well after an overlapped opening. Give the fresh uplink conditioner five
 * more capture blocks before that opening. AOQ still caps the rewind and
 * clamps it to the start of the current masked reply input. Only the replay
 * boundary moves; the vote onset and confirmation time do not. */
static inline uint32_t p4_near_replay_anchor(uint32_t onset,bool reference_gap)
{
    const uint32_t conditioner_lead_ms=5u*32u;
    return reference_gap && onset>conditioner_lead_ms?onset-conditioner_lead_ms:onset;
}

typedef struct {
    uint32_t frame_ms[16];
    uint32_t previous,first,second;
    unsigned next;
    bool have_previous,paired;
} p4_near_residual_onset_t;

/* Two earlier coherent frames across a syllable gap can retain missing
 * speech. They carry no votes, and only a corroborated residual burst may
 * use the pair. Store frame clocks to tolerate DMA batching jitter. */
static inline void p4_near_residual_onset_update(p4_near_residual_onset_t *s,
    uint32_t now,bool coherent)
{
    s->frame_ms[s->next++&15u]=now;
    if(!coherent)return;
    uint32_t gap=now-s->previous;
    if(s->have_previous && gap>=640u && gap<=1000u){
        s->first=s->previous;s->second=now;s->paired=true;
    }
    s->previous=now;s->have_previous=true;
}

static inline uint32_t p4_near_residual_onset_select(const p4_near_residual_onset_t *s,
    unsigned burst,uint32_t now,uint32_t onset)
{
    burst&=0xffffu;
    if(!burst || !s->paired || now-s->first>2000u)return onset;
    unsigned oldest=31u-__builtin_clz(burst);
    if(oldest>=s->next)return onset;
    uint32_t burst_start=s->frame_ms[(s->next-1u-oldest)&15u];
    uint32_t bridge=burst_start-s->second;
    return bridge>0 && bridge<=256u && (!onset || (int32_t)(s->first-onset)<0)?s->first:onset;
}
#endif
