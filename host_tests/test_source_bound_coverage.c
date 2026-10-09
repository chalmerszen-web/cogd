#include "source_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t records[SOURCE_METADATA_BYTES];
static void store_frame(unsigned index,unsigned raw,unsigned clean,bool spectral)
{
    uint8_t data[6]={(uint8_t)raw,(uint8_t)(raw>>8),(uint8_t)clean,
        (uint8_t)(clean>>8),spectral?1u:0u,0xa6};
    memcpy(records+index*6,data,6);
}
static void weak_tail(void)
{
    /* Two strict votes reset local silence at the edge of weak support.
     * The old source bound stops6000ms, before the consumer's6020ms. */
    source_bound_t source;agent_endpoint_t endpoint;
    assert(!source_bound_init(&source,100));source.end_grace_ms=AGENT_EP_PENDING_MS;
    assert(!agent_endpoint_init(&endpoint,AGENT_EP_FAST_END_MS,4000,10000));
    for(unsigned i=0;!source.target_samples;++i) {
        bool speech=i<200 || i==213 || i==215;
        agent_endpoint_observe(&endpoint,AGENT_EP_TEXT|AGENT_EP_PENDING);
        assert(!source_bound_feed(&source,speech?600:80));
        agent_endpoint_feed_supported(&endpoint,speech,speech);
    }
    assert(endpoint.state>=AGENT_EP_DONE && endpoint.elapsed_ms==6020);
    assert(source.elapsed_ms==6080 && endpoint.elapsed_ms<=source.elapsed_ms);
}
static void coverage(void)
{
    for(unsigned seed=1;seed<=1000;++seed) {
        source_bound_t source;agent_endpoint_t endpoint;
        assert(!source_bound_init(&source,159));source.end_grace_ms=AGENT_EP_PENDING_MS;
        assert(!agent_endpoint_init(&endpoint,AGENT_EP_FAST_END_MS,4000,10000));
        unsigned rng=seed,notice=0;
        for(unsigned i=0;!source.target_samples;++i) {
            rng=rng*1664525u+1013904223u;
            unsigned raw=i<seed%400+80?((rng>>8)&3)*200+80:80;
            store_frame(i,raw,raw>100?raw-100:raw,(rng&7)!=0);
            if(i%11==0)notice=agent_endpoint_notice(notice,false,false,true,false);
            if(i%7==0)notice=agent_endpoint_notice(notice,true,true,false,false);
            if(i%17==0)notice=agent_endpoint_proposal(notice,i*20);
            agent_endpoint_observe(&endpoint,notice);
            assert(!source_bound_feed(&source,raw));
            if(endpoint.state<AGENT_EP_DONE) {
                source_frame_t frame;
                assert(!source_stream_endpoint(&endpoint,records,(i+1)*320,159,&frame));
            }
        }
        assert(endpoint.state>=AGENT_EP_DONE && endpoint.elapsed_ms<=source.elapsed_ms);
        assert(endpoint.held_frames<=AGENT_EP_PENDING_MS/AGENT_VAD_FRAME_MS);
    }
}
int main(void)
{
    weak_tail();coverage();
    puts("Producer covers weak continuation and asynchronous endpoint notices: PASS");
    return 0;
}
