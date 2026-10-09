#include "endpoint_bridge.h"
#include "source_bound.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void init(agent_endpoint_t *e,agent_endpoint_bridge_t *b)
{assert(!agent_endpoint_init(e,700,4000,10000));assert(!agent_endpoint_bridge_init(e,b));}
static void frames(agent_endpoint_t *e,agent_endpoint_bridge_t *b,unsigned n,bool strong,bool energy,bool spectral)
{while(n--)agent_endpoint_bridge_feed(e,b,strong,energy,spectral);}
static void candidate(agent_endpoint_t *e,agent_endpoint_bridge_t *b)
{
    init(e,b);frames(e,b,20,true,true,true);
    agent_endpoint_bridge_observe(e,b,AGENT_EP_PENDING);
    frames(e,b,35,false,false,false);assert(b->active && e->state==AGENT_EP_SPEECH);
}
static void boundaries(void)
{
    agent_endpoint_t e,saved;agent_endpoint_bridge_t b,kept;
    init(&e,&b);frames(&e,&b,20,true,true,true);frames(&e,&b,35,false,false,false);
    assert(e.state==AGENT_EP_DONE && !b.pending_seen && e.elapsed_ms==1100);
    saved=e;kept=b;agent_endpoint_bridge_observe(&e,&b,AGENT_EP_PENDING);
    frames(&e,&b,50,true,true,true);assert(!memcmp(&e,&saved,sizeof(e)) && !memcmp(&b,&kept,sizeof(b)));

    init(&e,&b);agent_endpoint_bridge_observe(&e,&b,AGENT_EP_PENDING);
    frames(&e,&b,200,false,true,true);
    assert(e.state==AGENT_EP_NO_SPEECH && !b.bridge_at_ms && !b.active);

    candidate(&e,&b);frames(&e,&b,3,false,true,false);
    frames(&e,&b,1,false,true,true);frames(&e,&b,3,false,false,true);
    assert(b.bridge_at_ms==1240 && b.spent && !b.active && e.quiet_ms==0 && e.end_ms==700);
    unsigned at=b.bridge_at_ms;
    for(unsigned i=0;e.state<AGENT_EP_DONE;++i) {
        assert(i<100);agent_endpoint_bridge_observe(&e,&b,AGENT_EP_PENDING);
        frames(&e,&b,1,false,true,true);
    }
    assert(b.bridge_at_ms==at && e.elapsed_ms<=at+1700);

    candidate(&e,&b);frames(&e,&b,4,false,true,false);frames(&e,&b,4,false,false,true);
    assert(!b.bridge_at_ms && e.quiet_ms>700);
    while(e.state<AGENT_EP_DONE)frames(&e,&b,1,false,false,false);
    assert(!b.bridge_at_ms && b.spent && e.elapsed_ms==1900);

    candidate(&e,&b);frames(&e,&b,2,false,true,false);frames(&e,&b,1,false,true,true);
    frames(&e,&b,3,false,false,true);assert(!b.bridge_at_ms);
    candidate(&e,&b);frames(&e,&b,4,true,true,true);
    assert(b.resumed_at_ms==1180 && b.spent && !b.bridge_at_ms && e.end_ms==700);

    candidate(&e,&b);agent_endpoint_cancel(&e);saved=e;kept=b;
    frames(&e,&b,500,true,true,true);agent_endpoint_bridge_observe(&e,&b,AGENT_EP_PENDING);
    assert(!memcmp(&e,&saved,sizeof(e)) && !memcmp(&b,&kept,sizeof(b)));
    init(&e,&b);agent_endpoint_bridge_observe(&e,&b,AGENT_EP_PENDING);
    frames(&e,&b,500,true,true,true);assert(e.state==AGENT_EP_LIMIT && e.elapsed_ms==10000);
    assert(!agent_endpoint_init(&e,1000,4000,10000));
    assert(agent_endpoint_bridge_init(&e,&b)==AGENT_ERR_ARGUMENT);
}
static void coverage(unsigned seed)
{
    agent_endpoint_t e;agent_endpoint_bridge_t b;source_bound_t source;
    init(&e,&b);unsigned noise=80+seed%320;
    assert(!source_bound_init(&source,noise));source.end_grace_ms=AGENT_EP_PENDING_MS;
    unsigned rng=seed,history=0,notice=0;
    for(unsigned i=0;!source.target_samples;++i) {
        assert(i<500);rng=rng*1664525u+1013904223u;
        unsigned raw=i<seed%400+50?80+((rng>>8)&3u)*240:80;
        unsigned clean=raw>100?raw-100:raw;
        history=(history<<1)|((rng&7u)!=0);
        bool spectral=(history&(1u<<(seed%4)))!=0;
        unsigned strong=noise*2,weak=noise*3/2;
        if(strong<240)strong=240;
        if(weak<240)weak=240;
        if(i%11==0)notice=agent_endpoint_notice(notice,false,false,true,false);
        if(i%7==0)notice=agent_endpoint_notice(notice,true,true,false,false);
        agent_endpoint_bridge_observe(&e,&b,notice);
        assert(!source_bound_feed(&source,raw));
        agent_endpoint_bridge_feed(&e,&b,raw>strong && clean>strong,raw>weak && clean>weak,spectral);
    }
    assert(e.state>=AGENT_EP_DONE && e.elapsed_ms<=source.elapsed_ms && e.elapsed_ms<=10000);
}
int main(void)
{
    boundaries();for(unsigned seed=1;seed<=1000;++seed)coverage(seed);
    printf("boundary checks and1000 producer patterns pass; bridge state%zuB\n",sizeof(agent_endpoint_bridge_t));
    return 0;
}
