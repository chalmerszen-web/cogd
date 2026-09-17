#include "confirmation.h"
#include <string.h>

agent_err_t agent_confirmation_init(agent_confirmation_t *c,unsigned end,unsigned wait,unsigned maximum)
{
    if(!c) return AGENT_ERR_ARGUMENT;
    agent_endpoint_t endpoint;
    agent_err_t error=agent_endpoint_init(&endpoint,end,wait,maximum); if(error) return error;
    memset(c,0,sizeof(*c)); c->endpoint=endpoint; return AGENT_OK;
}
agent_err_t agent_confirmation_score(agent_confirmation_t *c,unsigned probability)
{
    if(!c || probability>32768) return AGENT_ERR_ARGUMENT;
    if(c->endpoint.state==AGENT_EP_CANCELLED) return AGENT_ERR_CANCELLED;
    if(c->confirmed || c->endpoint.state>=AGENT_EP_DONE) return AGENT_ERR_BUSY;
    if(c->frames>=625) return AGENT_ERR_LIMIT;
    unsigned at=c->frames%5; c->previous_sum=c->sum;
    c->sum=c->sum-c->recent[at]+probability; c->recent[at]=(uint16_t)probability;
    ++c->frames; return AGENT_OK;
}
agent_err_t agent_confirmation_feed(agent_confirmation_t *c,bool speech)
{
    if(!c) return AGENT_ERR_ARGUMENT;
    if(c->endpoint.state==AGENT_EP_CANCELLED) return AGENT_ERR_CANCELLED;
    if(c->endpoint.state>=AGENT_EP_DONE) return AGENT_OK;
    unsigned expected=(c->endpoint.elapsed_ms+AGENT_VAD_FRAME_MS)/16;
    if(!c->confirmed && c->frames<expected) return AGENT_ERR_BUSY;
    if(!c->confirmed && c->frames>expected+1) return AGENT_ERR_LIMIT;
    unsigned sum=c->frames==expected?c->sum:c->previous_sum;
    agent_endpoint_feed(&c->endpoint,speech);
    if(!c->confirmed && expected>=5 && sum>=65536 && speech) {
        unsigned votes=0;
        for(unsigned bits=c->endpoint.onset_bits;bits;bits>>=1) votes+=bits&1u;
        if(votes>=4) {
            agent_endpoint_support(&c->endpoint,true);
            if(c->endpoint.state==AGENT_EP_SPEECH && c->endpoint.speech_ms>=120) {
                c->confirmed=true; c->confirmed_ms=c->endpoint.elapsed_ms;
            }
        }
    }
    agent_endpoint_verify(&c->endpoint,c->confirmed);
    return AGENT_OK;
}
void agent_confirmation_cancel(agent_confirmation_t *c)
{ if(c) agent_endpoint_cancel(&c->endpoint); }
