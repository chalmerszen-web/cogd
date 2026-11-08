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
#endif
