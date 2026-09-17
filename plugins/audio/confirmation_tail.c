#include "confirmation_tail.h"

static bool original_limits(const agent_confirmation_t *c)
{
    return c->endpoint.end_ms==1000 && c->endpoint.wait_ms==4000 && c->endpoint.maximum_ms==10000 &&
        c->endpoint.elapsed_ms%20==0 && c->endpoint.elapsed_ms<=4000 && c->frames<=625;
}
agent_err_t confirmation_tail_open(confirmation_tail_t *t,const uint8_t *records,unsigned published,unsigned noise)
{
    if(!t || !records || noise>32768)return AGENT_ERR_ARGUMENT;
    if(published>160000)return AGENT_ERR_LIMIT;
    if(published<64000)return AGENT_ERR_BUSY;
    unsigned threshold=noise*3/2,bits=0,total=0,last=0;
    if(threshold<240)threshold=240;
    for(unsigned i=0;i<200;++i) {
        source_frame_t f;agent_err_t error=source_stream_read(records,SOURCE_METADATA_BYTES,published,i,&f);
        if(error)return error;
        bool possible=f.level>threshold;
        total+=possible;bits=((bits<<1)|(possible?1u:0u))&255u;
        unsigned votes=0;for(unsigned v=bits;v;v>>=1)votes+=v&1u;
        if(possible && total>=6 && votes>=4)last=(i+1)*20;
    }
    *t=(confirmation_tail_t){.records=records,.noise=noise,.last_possible_ms=last};
    return AGENT_OK;
}
agent_err_t confirmation_tail_begin(confirmation_tail_t *t,agent_confirmation_t *c)
{
    if(!t || !c)return AGENT_ERR_ARGUMENT;
    if(c->endpoint.state==AGENT_EP_CANCELLED)return AGENT_ERR_CANCELLED;
    if(!t->records)return AGENT_ERR_CONFIG;
    if(t->owner || c->confirmed || c->endpoint.state>=AGENT_EP_DONE)return AGENT_ERR_BUSY;
    if(!original_limits(c) || c->confirmed_ms!=0)return AGENT_ERR_ARGUMENT;
    unsigned expected=c->endpoint.elapsed_ms/16;
    if(c->frames<expected || c->frames>expected+1)return AGENT_ERR_ARGUMENT;
    if(c->endpoint.elapsed_ms<t->last_possible_ms || c->frames<(t->last_possible_ms+15)/16)return AGENT_ERR_BUSY;
    t->owner=c;t->frames=c->frames;t->next_ms=c->endpoint.elapsed_ms;
    return AGENT_OK;
}
agent_err_t confirmation_tail_step(confirmation_tail_t *t,agent_confirmation_t *c,source_frame_t *out)
{
    if(!t || !c || !out)return AGENT_ERR_ARGUMENT;
    if(c->endpoint.state==AGENT_EP_CANCELLED)return AGENT_ERR_CANCELLED;
    if(t->owner!=c || !t->records)return AGENT_ERR_CONFIG;
    if(c->confirmed || c->confirmed_ms || !original_limits(c) || c->frames!=t->frames || c->endpoint.elapsed_ms!=t->next_ms)return AGENT_ERR_ARGUMENT;
    if(c->endpoint.state>=AGENT_EP_DONE)return AGENT_ERR_BUSY;
    source_frame_t f;agent_err_t error=source_stream_read(t->records,SOURCE_METADATA_BYTES,64000,t->next_ms/20,&f);
    if(error)return error;
    unsigned threshold=c->endpoint.state==AGENT_EP_SPEECH?t->noise*3/2:t->noise*2;
    if(threshold<240)threshold=240;
    /* The possible-first-confirmation predicate has closed. Every future
     * unknown neural score leaves the confirmation branch without an effect.
     * Execute the original classical transition and verification, not a fake
     * probability and not a forced terminal state. */
    agent_endpoint_feed(&c->endpoint,f.spectral && f.level>threshold);
    agent_endpoint_verify(&c->endpoint,false);
    t->next_ms=c->endpoint.elapsed_ms;*out=f;
    return AGENT_OK;
}
