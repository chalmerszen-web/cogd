#ifndef P4_INPUT_BUFFER_H
#define P4_INPUT_BUFFER_H
#include <stdbool.h>
#include <stdint.h>

/* AOQ's receiver can keep advancing the input timeline after Audio datagrams
 * stop. Manual turns therefore maintain the empty buffer between utterances.
 * Never clear uncommitted speech, and hold new uploads until the clear ACK. */
typedef struct {
    uint64_t reset_at, sent_at;
    bool armed, pending, full;
} p4_input_buffer_t;
static inline void p4_input_buffer_committed(p4_input_buffer_t *s,uint64_t now)
{ s->armed=true;s->reset_at=now;s->full=false; }
static inline bool p4_input_buffer_due(const p4_input_buffer_t *s,uint64_t now,bool uncommitted)
{ return s->armed && !s->pending && !uncommitted && (s->full || now-s->reset_at>=20000); }
static inline void p4_input_buffer_sent(p4_input_buffer_t *s,uint64_t now)
{ s->pending=true;s->sent_at=now; }
static inline bool p4_input_buffer_cleared(p4_input_buffer_t *s,uint64_t now)
{
    if(!s->pending)return false;
    s->pending=false;s->full=false;s->reset_at=now;return true;
}
static inline bool p4_input_buffer_timeout(const p4_input_buffer_t *s,uint64_t now)
{ return s->pending && now-s->sent_at>=3000; }
#endif
