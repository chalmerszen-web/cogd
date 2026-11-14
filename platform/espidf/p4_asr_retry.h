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
typedef enum { P4_ASR_REPLAY_WAIT, P4_ASR_REPLAY_READY, P4_ASR_REPLAY_EXPIRED } p4_asr_replay_t;
/* Do not rewind before configuration/history ACKs. The producer is still
 * filling the ring during setup and a rewind would consume its free space.
 * The capture owner checks the live producer cursor and reserved space at
 * the actual rewind. On refusal it leaves the forward consumer untouched. */
static inline p4_asr_replay_t p4_asr_retry_replay(const p4_asr_retry_t *s,
    bool *waiting,bool input_ready,bool (*rewind)(unsigned))
{
    if(!*waiting || !input_ready)return P4_ASR_REPLAY_WAIT;
    *waiting=false;
    return rewind(s->start)?P4_ASR_REPLAY_READY:P4_ASR_REPLAY_EXPIRED;
}
/* Exclude acoustic pre-roll and endpoint silence from retry evidence. A
 * 220 ms candidate with an empty final must not become a fabricated word
 * merely because the retained buffer is nearly two seconds long. */
static inline bool p4_asr_retry_has_voice(unsigned start,unsigned onset,unsigned voice,unsigned end)
{
    unsigned span=end-start;
    return onset-start<=span && voice-onset>=6400u && voice-onset<=end-onset;
}
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
