#ifndef P4_AOQ_RECOVERY_H
#define P4_AOQ_RECOVERY_H
#include <stdbool.h>
#include <limits.h>
#include <stdint.h>

/* AOQ VAD timestamps count input audio within this connection. Translate
 * only endpoints covered by accepted 16 kHz PCM. Capture cursors may wrap;
 * cloud offsets and accepted counts must not. Invalid clocks keep the
 * caller's receipt-time fallback intact. */
static inline bool p4_aoq_capture_endpoint(unsigned first_cursor,
    unsigned accepted_samples,double cloud_ms,unsigned *endpoint)
{
    if(!(cloud_ms>=0.0) || cloud_ms>(double)(UINT_MAX/16u))return false;
    unsigned ms=(unsigned)cloud_ms;
    if((double)ms!=cloud_ms || ms*16u>accepted_samples)return false;
    *endpoint=first_cursor+ms*16u;
    return true;
}

/* A strict local double-talk confirmation can lag a quiet onset by over two
 * seconds. Retain up to three seconds of the existing AEC ring, bounded by
 * where playback muting began; never replay the preceding user request. */
static inline unsigned p4_aoq_local_replay(unsigned cursor,unsigned muted_from)
{
    unsigned samples=cursor-muted_from;
    if(samples>48000u)samples=48000u;
    return cursor-samples;
}
/* A fixed three-second rewind can replay a whole old speaker phrase before
 * the actual interruption, eliciting an empty cloud turn and another cancel.
 * Use the independently confirmed near-voice chain with 500 ms pre-roll.
 * Preserve the old bounded fallback for absent/stale clocks. Cursor time is
 * the audio-reader clock, not event receipt time or delayed network send time. */
static inline unsigned p4_aoq_local_onset_replay(unsigned cursor,unsigned muted_from,
    uint32_t capture_origin_ms,uint32_t onset_ms)
{
    unsigned fallback=p4_aoq_local_replay(cursor,muted_from);
    if(!onset_ms)return fallback;
    uint32_t reader_ms=capture_origin_ms+cursor/16u;
    int32_t age=(int32_t)(reader_ms-onset_ms);
    if(age< -100 || age>3000)return fallback;
    unsigned keep=(unsigned)(age>0?age:0)+500u;
    if(keep>3000u)keep=3000u;
    unsigned samples=keep*16u;
    if(samples>cursor-muted_from)samples=cursor-muted_from;
    return cursor-samples;
}
/* A cloud start can arrive about two seconds after the captured onset. Keep
 * a small acoustic pre-roll within this connection, bounded by the same
 * three-second window as local confirmation. Unmapped events retain the old
 * 750 ms fallback; never guess a cloud clock outside accepted PCM. */
static inline unsigned p4_aoq_cloud_replay(unsigned cursor,unsigned first_cursor,
    unsigned onset,bool onset_mapped,unsigned endpoint)
{
    unsigned samples=cursor-(onset_mapped?onset:endpoint);
    if(onset_mapped){
        if(samples>48000u)samples=48000u;
        else samples+=1600u;
        if(samples>cursor-first_cursor)samples=cursor-first_cursor;
    }
    unsigned limit=onset_mapped?48000u:12000u;
    if(samples>limit)samples=limit;
    return cursor-samples;
}
/* A mapped cloud endpoint predates event delivery and any upload cue. It
 * must win over cue muting AND receipt-time caps: during accelerated replay
 * the reader can already be several seconds into the next clause. Clamping
 * that validated boundary silently discards real words. The capture ring's
 * rewind guard checks its actual retention window and discontinuities; an
 * expired mapped boundary must fail there rather than upload a truncated
 * utterance. Without a valid mapping, retain the bounded fallback. */
static inline unsigned p4_aoq_cancel_replay(unsigned cursor,unsigned endpoint,
    unsigned muted_from,bool muted,bool endpoint_mapped)
{
    if(endpoint_mapped)return endpoint;
    unsigned samples=cursor-(muted?muted_from:endpoint);
    unsigned limit=muted?12000u:35200u;
    if(samples>limit)samples=limit;
    return cursor-samples;
}
typedef struct {unsigned completed_turns,cancellations;} p4_aoq_recovery_t;
/* Repeated provider cancellations with no completed reply must not allocate
 * indefinitely. A completed turn starts a fresh recovery allowance. */
static inline bool p4_aoq_cancel_retry(p4_aoq_recovery_t *s,unsigned turns)
{
    if(s->completed_turns!=turns){s->completed_turns=turns;s->cancellations=0;}
    if(s->cancellations>=3)return false;
    ++s->cancellations;return true;
}
#endif
