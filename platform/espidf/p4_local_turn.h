#ifndef P4_LOCAL_TURN_H
#define P4_LOCAL_TURN_H
#include <stdbool.h>
#include <stdint.h>

/* All positions are 16 kHz capture sample cursors, never upload/loop time.
 * The caller sends the endpoint frame before committing. BUSY retries and
 * onset replay may revisit frames; neither can advance the silence clock. */
typedef struct {
    unsigned votes, onset, start, last_voice, analysed, floor;
    unsigned hint_first,hint_last;
    bool active, ended, hint;
} p4_local_turn_t;
typedef enum { P4_LOCAL_NONE, P4_LOCAL_START, P4_LOCAL_END } p4_local_event_t;
static inline unsigned p4_local_onset_level(unsigned quiet_level)
{
    /* 6 dB above the measured idle floor. Only admission uses this value;
     * quieter following consonants remain inside the complete active turn. */
    unsigned level=quiet_level*2u;
    return level>280?level:280;
}
static inline unsigned p4_local_continuation_level(unsigned quiet_level)
{
    /* Keep hysteresis below admission, but never let amplified idle noise
     * refresh last_voice indefinitely. All PCM, including quiet consonants,
     * still uploads until the normal 600 ms endpoint after genuine speech. */
    unsigned level=quiet_level+quiet_level/2u;
    return level>140?level:140;
}
typedef enum { P4_LOCAL_WAIT, P4_LOCAL_REJECT, P4_LOCAL_RESPOND } p4_local_reply_t;
static inline p4_local_reply_t p4_local_reply(bool committed,bool transcribed,bool has_text,bool resolved)
{
    if(!committed || !transcribed || resolved)return P4_LOCAL_WAIT;
    return has_text?P4_LOCAL_RESPOND:P4_LOCAL_REJECT;
}

static inline void p4_local_turn_reset(p4_local_turn_t *s,unsigned floor)
{ *s=(p4_local_turn_t){.floor=floor}; }
static inline void p4_local_turn_seed(p4_local_turn_t *s,unsigned start,unsigned confirmed)
{
    *s=(p4_local_turn_t){.start=start,.last_voice=confirmed,.analysed=start,.active=true,.floor=start};
}
static inline void p4_local_turn_seed_replay(p4_local_turn_t *s,unsigned start,unsigned confirmed)
{
    /* Preserve the actual confirmed sample boundary across reconnect. A
     * three-second replay must not end after a fabricated 500 ms seed plus
     * 600 ms silence, before it reaches the real interruption at all. */
    if(!confirmed || confirmed-start>48000u)confirmed=start+8000u;
    p4_local_turn_seed(s,start,confirmed);
}
/* A weak onset can retain PCM but cannot admit a turn. Require the original
 * six strong votes before sending anything. Bound the hint to 1.2 seconds,
 * expire separated noise pulses, and never cross the prior committed floor. */
static inline void p4_local_turn_hint(p4_local_turn_t *s,unsigned end,bool candidate,bool allow)
{
    if(s->active || s->ended || end<=s->analysed)return;
    if(!allow){s->hint=false;return;}
    if(!candidate)return;
    if(!s->hint || end-s->hint_last>6400u){s->hint_first=end>=320?end-320:0;s->hint=true;}
    s->hint_last=end;
    if(end-s->hint_first>19200u)s->hint_first=end-19200u;
}
static inline p4_local_event_t p4_local_turn_feed(p4_local_turn_t *s,unsigned end,
    bool voiced,bool allow_start,unsigned silence_ms)
{
    if(s->ended || end<=s->analysed)return P4_LOCAL_NONE;
    s->analysed=end;
    if(s->active) {
        if(voiced && end>s->last_voice)s->last_voice=end;
        if(end>=s->last_voice && end-s->last_voice>=silence_ms*16u) {
            s->ended=true;return P4_LOCAL_END;
        }
        return P4_LOCAL_NONE;
    }
    if(!allow_start){s->votes=0;return P4_LOCAL_NONE;}
    s->votes=((s->votes<<1)|(unsigned)voiced)&1023u;
    unsigned count=0,span=0;
    for(unsigned bits=s->votes;bits;bits>>=1){count+=bits&1u;++span;}
    if(count<6)return P4_LOCAL_NONE;
    /* Sparse old pulses must not pin onset before this bounded vote window. */
    s->onset=end>=span*320?end-span*320:0;
    s->start=s->onset>8000?s->onset-8000:0;
    if(s->hint && end>=s->hint_last && end-s->hint_last<=6400u) {
        unsigned first=s->hint_first;
        if(end-first>19200u)first=end-19200u;
        unsigned earlier=first>2048?first-2048:0;
        if(earlier<s->start)s->start=earlier;
    }
    if(s->start<s->floor)s->start=s->floor;
    s->last_voice=end;s->active=true;
    return P4_LOCAL_START;
}
#endif
