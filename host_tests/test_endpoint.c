#include "endpoint.h"
#include <assert.h>
#include <stdio.h>

static void feed(agent_endpoint_t *e,unsigned ms,bool voice)
{ for(unsigned n=0;n<ms;n+=20) agent_endpoint_feed(e,voice); }
int main(void)
{
    agent_endpoint_t e;
    assert(agent_endpoint_init(&e,200,4000,10000)==AGENT_ERR_ARGUMENT);
    assert(!agent_endpoint_init(&e,800,4000,10000));
    feed(&e,3980,false); assert(e.state==AGENT_EP_WAIT);
    feed(&e,20,false); assert(e.state==AGENT_EP_NO_SPEECH);
    feed(&e,1000,true); assert(e.state==AGENT_EP_NO_SPEECH && e.elapsed_ms==4000);
    assert(!agent_endpoint_init(&e,1000,4000,10000));
    /* Six accepted transient frames were observed without new command speech.
     * They must not publish a completed utterance one second later. */
    feed(&e,40,false); feed(&e,120,true); feed(&e,3840,false);
    assert(e.state==AGENT_EP_NO_SPEECH && e.elapsed_ms==4000);
    assert(!agent_endpoint_init(&e,800,4000,10000));
    /* Isolated transients do not arm the endpoint. */
    for(unsigned i=0;i<30;++i) { feed(&e,20,true); feed(&e,80,false); }
    assert(e.state==AGENT_EP_WAIT);
    feed(&e,200,true); assert(e.state==AGENT_EP_SPEECH);
    feed(&e,400,false); assert(e.state==AGENT_EP_SPEECH);
    feed(&e,400,true); feed(&e,780,false); assert(e.state==AGENT_EP_SPEECH);
    feed(&e,20,false); assert(e.state==AGENT_EP_DONE);
    assert(!agent_endpoint_init(&e,1000,4000,10000));
    feed(&e,400,true); feed(&e,200,false);
    for(unsigned i=0;i<8;++i) { feed(&e,80,false); feed(&e,20,true); }
    assert(e.state==AGENT_EP_DONE && e.elapsed_ms==1400); /* Isolated noise cannot keep a finished utterance open. */
    assert(!agent_endpoint_init(&e,1000,4000,10000));
    feed(&e,200,true); feed(&e,200,false);
    /* Repeated 60-ms background bursts after speech must not force the clip
     * to its hard limit. They never form a sustained new spoken segment. */
    for(unsigned i=0;i<12;++i) { feed(&e,60,true); feed(&e,100,false); }
    assert(e.state==AGENT_EP_DONE && e.elapsed_ms==1200);
    assert(!agent_endpoint_init(&e,800,4000,10000));
    feed(&e,9980,true); assert(e.state==AGENT_EP_SPEECH);
    feed(&e,20,true); assert(e.state==AGENT_EP_LIMIT);
    assert(!agent_endpoint_init(&e,800,4000,10000));
    agent_endpoint_cancel(&e); feed(&e,10000,true);
    assert(e.state==AGENT_EP_CANCELLED && !e.elapsed_ms);
    agent_endpoint_verify(&e,false); assert(e.state==AGENT_EP_CANCELLED);
    /* Independent rejection must not postpone the waiting deadline, even if
     * the spectral detector calls uninterrupted background noise speech. */
    assert(!agent_endpoint_init(&e,1000,4000,10000));
    for(unsigned ms=0;ms<4000;ms+=20) {
        agent_endpoint_feed(&e,true); agent_endpoint_verify(&e,false);
    }
    assert(e.state==AGENT_EP_NO_SPEECH && e.elapsed_ms==4000);
    agent_endpoint_verify(&e,true); assert(e.state==AGENT_EP_NO_SPEECH);
    /* A rejected early transient leaves time for a real, later short word. */
    assert(!agent_endpoint_init(&e,1000,4000,10000));
    for(unsigned ms=0;ms<1800;ms+=20) {
        agent_endpoint_feed(&e,ms<200); agent_endpoint_verify(&e,false);
    }
    assert(e.state==AGENT_EP_WAIT && e.elapsed_ms==1800);
    for(unsigned ms=0;ms<1200;ms+=20) {
        agent_endpoint_feed(&e,ms<200); agent_endpoint_verify(&e,ms>=100);
    }
    assert(e.state==AGENT_EP_DONE && e.elapsed_ms==3000);
    /* Fragmented real speech needs independent strong evidence to relax onset.
     * Six votes in eight frames alone still take the original no-speech path. */
    const bool fragmented[8]={true,true,false,true,true,false,true,true};
    for(unsigned support=0;support<2;++support) {
        assert(!agent_endpoint_init(&e,1000,4000,10000));
        for(unsigned i=0;i<8;++i) {
            agent_endpoint_feed(&e,fragmented[i]);
            agent_endpoint_support(&e,support!=0);
        }
        assert(e.state==(support?AGENT_EP_SPEECH:AGENT_EP_WAIT));
        feed(&e,1000,false);
        assert(e.state==(support?AGENT_EP_DONE:AGENT_EP_WAIT));
        if(support) assert(e.elapsed_ms==1160 && e.quiet_ms==1000);
    }
    /* Old cumulative activity cannot make four recent votes sufficient. */
    assert(!agent_endpoint_init(&e,1000,4000,10000));
    for(unsigned i=0;i<16;++i) {
        agent_endpoint_feed(&e,i%2==0);
        assert(!agent_endpoint_support(&e,true));
    }
    assert(e.state==AGENT_EP_WAIT);
    feed(&e,3680,false);assert(e.state==AGENT_EP_NO_SPEECH);
    assert(!agent_endpoint_support(&e,true) && e.state==AGENT_EP_NO_SPEECH);
    /* Short-command model confirmation follows the spectral onset. Waiting
     * for that evidence must not move the last-activity time or extend a clip. */
    for(unsigned delay=0;delay<=AGENT_EP_SUPPORT_MS+20;delay+=20) {
        assert(!agent_endpoint_init(&e,1000,4000,10000));
        feed(&e,120,true); feed(&e,delay,false);
        bool within=delay<=AGENT_EP_SUPPORT_MS;
        assert(agent_endpoint_support(&e,true)==within);
        assert(e.quiet_ms==delay);
        feed(&e,1000-delay,false);
        assert(e.state==(within?AGENT_EP_DONE:AGENT_EP_WAIT));
        assert(e.elapsed_ms==1120);
    }
    /* A hundred-ms candidate may be followed by another syllable. Model
     * support still needs 120 ms total; an old four-vote history is insufficient. */
    assert(!agent_endpoint_init(&e,1000,4000,10000));
    feed(&e,100,true);feed(&e,160,false);
    assert(!agent_endpoint_support(&e,true));
    feed(&e,20,true); assert(agent_endpoint_support(&e,true));
    /* Weak evidence cannot use a candidate. A rejected burst clears it. */
    assert(!agent_endpoint_init(&e,1000,4000,10000));
    feed(&e,140,true);feed(&e,1000,false);
    assert(e.state==AGENT_EP_DONE && e.support_at_ms);
    agent_endpoint_verify(&e,false);
    assert(e.state==AGENT_EP_WAIT && !e.support_at_ms);
    assert(!agent_endpoint_support(&e,true));
    assert(!agent_endpoint_init(&e,1000,4000,10000));
    agent_endpoint_cancel(&e);assert(!agent_endpoint_support(&e,true) && e.state==AGENT_EP_CANCELLED);
    agent_activity_t a={0}; int16_t frame[320];
    for(unsigned i=0;i<320;++i) frame[i]=i%2?150:-150;
    for(unsigned i=0;i<40;++i) assert(!agent_activity_feed(&a,frame,320,true,false,false));
    assert(a.noise==150 && !agent_activity_feed(&a,frame,320,false,true,false));
    for(unsigned i=0;i<320;++i) frame[i]=i%2?500:-500;
    assert(agent_activity_feed(&a,frame,320,false,true,false) && a.noise==150);
    assert(!agent_activity_feed(&a,frame,320,false,false,true));
    for(unsigned i=0;i<320;++i) frame[i]=i%2?280:-280;
    assert(!agent_activity_feed(&a,frame,320,false,true,false));
    assert(agent_activity_feed(&a,frame,320,false,true,true)); /* Soft syllable after onset. */
    for(unsigned i=0;i<320;++i) frame[i]=i%2?150:-150;
    assert(!agent_activity_feed(&a,frame,320,false,true,true)); /* Room noise still ends speech. */
    puts("endpoint: transient rejection, pause tolerance, end timing, no-speech, hard bound and cancel passed");
}
