#include "source_bound.h"
#include <string.h>
agent_err_t source_bound_init(source_bound_t *b,unsigned noise)
{
    if(!b || noise>32768)return AGENT_ERR_ARGUMENT;
    memset(b,0,sizeof(*b));b->threshold=noise*3/2;
    if(b->threshold<240)b->threshold=240;
    return AGENT_OK;
}
agent_err_t source_bound_feed(source_bound_t *b,unsigned level)
{
    if(!b || level>32768)return AGENT_ERR_ARGUMENT;
    if(b->target_samples)return AGENT_ERR_BUSY;
    if(b->elapsed_ms>=10000)return AGENT_ERR_LIMIT;
    b->elapsed_ms+=20;
    bool potential=level>b->threshold;
    if(potential)++b->possible;
    b->onset_bits=((b->onset_bits<<1)|(potential?1u:0u))&255u;
    unsigned votes=0;for(unsigned bits=b->onset_bits;bits;bits>>=1)votes+=bits&1u;
    if(potential && votes>=4)b->last_continuation_ms=b->elapsed_ms;
    /* Actual accepted frames are a subset of these permissive energy bits;
     * unconfirmed resets only remove actual bits. Both original confirmation
     * and quiet-timer reset require current speech and>=4 recent actual votes.
     * They therefore imply a potential four-vote event here. After4000ms all
     * unconfirmed paths are terminal. A confirmed path has>=120ms speech and
     * must finish after1000ms with no possible reset. Neither an isolated
     * impulse nor future speech after this proven bound can reopen a path. */
    if(b->elapsed_ms>=4000 && (b->possible<6 || b->elapsed_ms-b->last_continuation_ms>=1000 || b->elapsed_ms==10000))
        b->target_samples=((b->elapsed_ms*16+255)/256)*256;
    return AGENT_OK;
}
