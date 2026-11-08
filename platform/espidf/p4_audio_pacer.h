#ifndef COGD_P4_AUDIO_PACER_H
#define COGD_P4_AUDIO_PACER_H
#include <stdint.h>

/* Advance the media deadline, not completion-time plus another whole frame.
 * Small processing/scheduler overruns must not accumulate through a phrase.
 * Rebase after a full missed interval and keep at least half an interval
 * between packets: a delayed owner must not flush a burst of old PCM. */
static inline uint64_t p4_audio_pacer_next(uint64_t due,uint64_t completed,unsigned interval)
{
    if(!due || completed<due || completed-due>=interval)return completed+interval;
    uint64_t next=due+interval,earliest=completed+(interval+1)/2;
    return next>earliest?next:earliest;
}
#endif
