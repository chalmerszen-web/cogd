#include "confirmation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void feed(agent_confirmation_t *c,bool voice,unsigned score,unsigned lead)
{
    unsigned wanted=(c->endpoint.elapsed_ms+20)/16+lead;
    while(!c->confirmed && c->endpoint.state<AGENT_EP_DONE && c->frames<wanted)
        assert(!agent_confirmation_score(c,score));
    assert(!agent_confirmation_feed(c,voice));
}
int main(void)
{
    agent_confirmation_t c,saved;
    assert(agent_confirmation_init(NULL,1000,4000,10000)==AGENT_ERR_ARGUMENT);
    assert(!agent_confirmation_init(&c,1000,4000,10000)); saved=c;
    assert(agent_confirmation_feed(&c,true)==AGENT_ERR_BUSY && !memcmp(&c,&saved,sizeof(c)));
    assert(agent_confirmation_score(&c,32769)==AGENT_ERR_ARGUMENT && !memcmp(&c,&saved,sizeof(c)));
    /* A score from144ms must not confirm the earlier140ms classical frame. */
    const unsigned q[10]={0,0,0,0,0,0,32768,32767,1,0};
    for(unsigned ms=20;ms<=140;ms+=20) {
        unsigned wanted=ms/16+1;
        while(c.frames<wanted) assert(!agent_confirmation_score(&c,q[c.frames]));
        assert(!agent_confirmation_feed(&c,true) && !c.confirmed);
    }
    assert(c.sum==65536 && c.previous_sum==65535);
    assert(!agent_confirmation_score(&c,q[9]));
    assert(!agent_confirmation_feed(&c,true) && c.confirmed && c.confirmed_ms==160);
    assert(agent_confirmation_score(&c,32768)==AGENT_ERR_BUSY);
    for(unsigned lead=0;lead<=1;++lead) {
        /* Neural-only positives neither latch nor create a success result. */
        assert(!agent_confirmation_init(&c,1000,4000,10000));
        for(unsigned ms=0;ms<4000;ms+=20) feed(&c,false,32768,lead);
        assert(!c.confirmed && c.endpoint.state==AGENT_EP_NO_SPEECH && c.endpoint.elapsed_ms==4000);
        saved=c; assert(!agent_confirmation_feed(&c,true) && !memcmp(&c,&saved,sizeof(c)));
        assert(!agent_confirmation_init(&c,1000,4000,10000));
        for(unsigned ms=0;ms<4000;ms+=20) feed(&c,true,0,lead);
        assert(!c.confirmed && c.endpoint.state==AGENT_EP_NO_SPEECH);
        const bool fragmented[8]={true,true,false,true,true,false,true,true};
        assert(!agent_confirmation_init(&c,1000,4000,10000));
        for(unsigned i=0;i<8;++i) feed(&c,fragmented[i],16384,lead);
        assert(c.confirmed && c.endpoint.state==AGENT_EP_SPEECH && c.confirmed_ms==160);
        unsigned scores=c.frames;
        for(unsigned ms=0;ms<1000;ms+=20) feed(&c,false,0,lead);
        assert(c.frames==scores && c.endpoint.state==AGENT_EP_DONE && c.endpoint.elapsed_ms==1160);
        assert(!agent_confirmation_init(&c,1000,4000,10000));
        for(unsigned ms=0;ms<200;ms+=20) feed(&c,true,16384,lead);
        for(unsigned ms=0;ms<400;ms+=20) feed(&c,false,0,lead);
        assert(c.endpoint.state==AGENT_EP_SPEECH);
        for(unsigned ms=0;ms<200;ms+=20) feed(&c,true,0,lead);
        for(unsigned ms=0;ms<1000;ms+=20) feed(&c,false,0,lead);
        assert(c.endpoint.state==AGENT_EP_DONE && c.endpoint.elapsed_ms==1800);
        assert(!agent_confirmation_init(&c,1000,4000,10000));
        for(unsigned ms=0;ms<10000;ms+=20) feed(&c,true,16384,lead);
        assert(c.confirmed && c.endpoint.state==AGENT_EP_LIMIT && c.endpoint.elapsed_ms==10000);
    }
    assert(!agent_confirmation_init(&c,1000,4000,10000));
    feed(&c,true,32768,0); agent_confirmation_cancel(&c); saved=c;
    assert(agent_confirmation_feed(&c,true)==AGENT_ERR_CANCELLED);
    assert(agent_confirmation_score(&c,32768)==AGENT_ERR_CANCELLED && !memcmp(&c,&saved,sizeof(c)));
    assert(!agent_confirmation_init(&c,1000,4000,10000));
    for(unsigned i=0;i<3;++i) assert(!agent_confirmation_score(&c,32768));
    saved=c; assert(agent_confirmation_feed(&c,true)==AGENT_ERR_LIMIT && !memcmp(&c,&saved,sizeof(c)));
    puts("confirmation: source clocks, future-score exclusion, joint evidence, pause, bounds and cancellation passed");
}
