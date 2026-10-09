#include "endpoint_bridge.h"
#include <string.h>

enum { OBSERVE_MS=800, WINDOW_MASK=255, VOTES=4 };
static unsigned count(unsigned bits)
{
    unsigned total=0;
    for(;bits;bits>>=1)total+=bits&1u;
    return total;
}
agent_err_t agent_endpoint_bridge_init(agent_endpoint_t *e,agent_endpoint_bridge_t *b)
{
    if(!e || !b || e->state!=AGENT_EP_WAIT || e->elapsed_ms ||
       e->end_ms!=AGENT_EP_FAST_END_MS || e->wait_ms!=4000 || e->maximum_ms!=10000)
        return AGENT_ERR_ARGUMENT;
    memset(b,0,sizeof(*b));return AGENT_OK;
}
void agent_endpoint_bridge_observe(agent_endpoint_t *e,agent_endpoint_bridge_t *b,unsigned notice)
{
    if(!e || !b || e->state>=AGENT_EP_DONE)return;
    if((notice&AGENT_EP_PENDING) && !b->spent) {
        b->pending_seen=true;e->end_ms=AGENT_EP_FAST_END_MS+OBSERVE_MS;
    }
    agent_endpoint_observe(e,notice);
}
agent_endpoint_state_t agent_endpoint_bridge_feed(agent_endpoint_t *e,agent_endpoint_bridge_t *b,
                                                bool strong,bool energy,bool spectral)
{
    if(!e || !b)return AGENT_EP_CANCELLED;
    if(e->state>=AGENT_EP_DONE)return e->state;
    b->energy_bits=(uint8_t)((b->energy_bits<<1)|(energy?1u:0u));
    b->spectral_bits=(uint8_t)((b->spectral_bits<<1)|(spectral?1u:0u));
    agent_endpoint_feed_supported(e,strong && spectral,energy && spectral);
    if(!b->pending_seen || b->spent || e->state>=AGENT_EP_DONE)return e->state;
    if(!b->active && e->state==AGENT_EP_SPEECH && e->speech_ms>=120 &&
       e->quiet_ms>=AGENT_EP_FAST_END_MS) {
        b->active=true;b->candidate_at_ms=(uint16_t)e->elapsed_ms;
    }
    if(!b->active)return e->state;
    if(e->dense_at_ms>b->candidate_at_ms && !e->quiet_ms) {
        b->resumed_at_ms=(uint16_t)e->elapsed_ms;
        b->spent=true;b->active=false;e->end_ms=AGENT_EP_FAST_END_MS;
    } else {
        unsigned recent=(1u<<(AGENT_EP_RESUME_MS/AGENT_VAD_FRAME_MS+1u))-1u;
        if(spectral && count(b->energy_bits&WINDOW_MASK)>=VOTES &&
           count(b->spectral_bits&WINDOW_MASK)>=VOTES && (b->energy_bits&recent)) {
            b->bridge_at_ms=(uint16_t)e->elapsed_ms;e->quiet_ms=0;
            b->spent=true;b->active=false;e->end_ms=AGENT_EP_FAST_END_MS;
        } else if(e->elapsed_ms-b->candidate_at_ms>=OBSERVE_MS) {
            b->spent=true;b->active=false;e->end_ms=AGENT_EP_FAST_END_MS;
            if(e->quiet_ms>=AGENT_EP_FAST_END_MS)e->state=AGENT_EP_DONE;
        }
    }
    return e->state;
}
