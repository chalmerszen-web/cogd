#ifndef P4_UPLINK_GATE_H
#define P4_UPLINK_GATE_H
#include <stdbool.h>
#include <stdint.h>

/* Cover DAC queueing, AEC overlap and capture block alignment after the cue.
 * Compare capture time, not send time: queued user PCM predates playback.
 * A cancelled cue with no end publication must never cause a permanent mute. */
static inline bool p4_uplink_cue_frame(uint32_t captured_ms,uint32_t begin_ms,uint32_t end_ms)
{
    if(!begin_ms)return false;
    int32_t from_begin=(int32_t)(captured_ms-begin_ms);
    if(from_begin<=-20 || from_begin>=800)return false;
    if(!end_ms || (int32_t)(end_ms-begin_ms)<0)return true;
    return (int32_t)(captured_ms-end_ms)<224;
}
static inline bool p4_uplink_reply_frame(uint64_t captured_us,uint64_t playback_started_ms)
{
    return playback_started_ms && captured_us+20000>playback_started_ms*1000;
}
#endif
