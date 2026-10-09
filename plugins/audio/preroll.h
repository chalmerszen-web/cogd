#ifndef AGENT_PREROLL_H
#define AGENT_PREROLL_H
#include "agent.h"

enum {
    AGENT_PREROLL_SAMPLES=512, AGENT_PREROLL_FRAMES=64,
    AGENT_PREROLL_PACKET=264,
    AGENT_PREROLL_BYTES=AGENT_PREROLL_FRAMES*AGENT_PREROLL_PACKET
};
typedef struct {
    uint8_t *data;
    uint64_t last_sample;
    unsigned next,count,read_at,remaining,discontinuities;
    int index;
    bool sealed;
} agent_preroll_t;
_Static_assert(sizeof(agent_preroll_t)<=128,"State must fit the idle prefix remainder");

/* Zero-initialize before init. Only the first AGENT_PREROLL_BYTES are borrowed;
 * unused capacity may hold the disjoint, aligned state. No I/O or allocation.
 * This module is NOT a concurrent lease manager: the adapter owns the arena,
 * serializes all writes, seals before publishing to a reader, and joins every
 * borrower before reset/reclaim. Existing engine/candidate buffers cannot be
 * lent without adding this lifecycle to all of their reuse paths. */
agent_err_t agent_preroll_init(agent_preroll_t *,void *storage,size_t capacity);
/* One actual 16kHz block, ending on its monotonic512-sample source clock.
 * A forward gap discards old history; duplicates/backward clocks are rejected.
 * First sample is exact; other samples use the existing lossy IMA4-bit codec. */
agent_err_t agent_preroll_feed(agent_preroll_t *,const int16_t pcm[AGENT_PREROLL_SAMPLES],uint64_t sample_end);
/* Validate the entire retained history before any ASR reader can see it. */
agent_err_t agent_preroll_seal(agent_preroll_t *);
/* Read one complete chronological block. NOT_FOUND denotes EOF. Failed reads
 * leave output and cursor unchanged. Source timestamps never include padding. */
agent_err_t agent_preroll_read(agent_preroll_t *,int16_t pcm[AGENT_PREROLL_SAMPLES],uint64_t *sample_end);
/* Caller has joined readers/writer, including cancellation. Storage is not
 * freed or cleared; reset ends the loan and permits a later explicit init. */
void agent_preroll_reset(agent_preroll_t *);
#endif
