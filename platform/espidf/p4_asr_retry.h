#ifndef P4_ASR_RETRY_H
#define P4_ASR_RETRY_H
#include <stdbool.h>
#include <stdint.h>

/* An empty final must not silently discard a confirmed short utterance.
 * Re-submit retained capture on a NEW connection: AOQ capture timestamps
 * cannot go backwards within a stream. Each capture interval gets one retry;
 * an earlier empty noise candidate must not consume a later question's retry.
 * Cap consecutive retries at three until a real nonempty final is accepted,
 * so persistent noise cannot reconnect forever.
 * Leave three seconds of the 12-second microphone ring for reconnection. */
typedef struct {
    unsigned start,voice,end,attempts;
    bool pending;
} p4_asr_retry_t;
static inline bool p4_asr_retry_begin(p4_asr_retry_t *s,unsigned start,
                                    unsigned voice,unsigned end)
{
    unsigned span=end-start;
    if(s->attempts>=3 || (s->attempts && (int32_t)(start-s->end)<0) ||
       span<3200u || span>128000u ||
       voice-start<1920u || voice-start>span)return false;
    *s=(p4_asr_retry_t){.start=start,.voice=voice,.end=end,.attempts=s->attempts+1,.pending=true};
    return true;
}
static inline void p4_asr_retry_accepted(p4_asr_retry_t *s)
{ s->attempts=0;s->pending=false; }
#endif
