#ifndef P4_LIVE_INPUT_H
#define P4_LIVE_INPUT_H
#include <stdbool.h>
#include <stdint.h>

/* Track accepted microphone samples independently of speech admission.
 * Quiet live input may precede VAD, but never authorizes a response. Once
 * admitted, those samples must not be rewound and uploaded a second time. */
typedef struct { bool active; unsigned first, end; } p4_live_input_t;
static inline bool p4_live_input_start(bool permitted,bool speech,bool hint,
                                      uint64_t capture_us,uint64_t now_us)
{
    return permitted && !speech && !hint && now_us>=capture_us &&
           now_us-capture_us<=80000;
}
/* Masked speaker/cue frames still occupy the open input timeline. Dropping
 * them creates a hole when listening resumes; ended turns remain frozen. */
static inline bool p4_live_input_continue(const p4_live_input_t *s,bool ended,bool available)
{ return s->active && !ended && available; }
static inline bool p4_live_input_accept(p4_live_input_t *s,unsigned end,unsigned samples)
{
    if(!samples || end<samples || (s->active && end-samples!=s->end))return false;
    if(!s->active){s->active=true;s->first=end-samples;}
    s->end=end;return true;
}
static inline bool p4_live_input_contains(const p4_live_input_t *s,unsigned cursor)
{ return s->active && cursor>=s->first && cursor<=s->end; }
#endif
