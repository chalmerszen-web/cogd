#include "source_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t records[SOURCE_METADATA_BYTES];
static agent_endpoint_t endpoint;
static bool no_speech(void *ctx,int16_t *pcm){(void)ctx;(void)pcm;return false;}
static void reset(void){memset(records,0,sizeof(records));assert(!agent_endpoint_init(&endpoint,1600,4000,10000));}
static void frame(unsigned level,bool spectral)
{
    unsigned index=endpoint.elapsed_ms/20;
    uint8_t r[6]={(uint8_t)level,(uint8_t)(level>>8),(uint8_t)level,(uint8_t)(level>>8),spectral,0xa6};
    memcpy(records+index*6,r,6);source_frame_t out;
    assert(!source_stream_endpoint(&endpoint,records,(index+1)*320,197,&out));
    agent_endpoint_observe(&endpoint,endpoint.asr_notice);
}
int main(void)
{
    /* A loud onset followed by quiet, recognized continuation used to let
     * acquisition stop near 5 s while the endpoint still needed PCM to 6 s. */
    source_stream_t producer;int16_t filtered[SOURCE_FRAME_SAMPLES];
    assert(!source_stream_init(&producer,records,sizeof(records),filtered,197,no_speech,NULL));
    producer.fast_guard=true;producer.bound.end_grace_ms=AGENT_EP_PENDING_MS;
    for(unsigned i=0;i<SOURCE_FRAME_SAMPLES;i++)assert(!source_stream_feed(&producer,0));
    source_bound_t bound=producer.bound;
    reset();agent_endpoint_observe(&endpoint,AGENT_EP_TEXT);frame(159,false);
    while(endpoint.state<AGENT_EP_DONE) {
        unsigned t=endpoint.elapsed_ms;
        unsigned level=t<1500?159:t<2000?500:t<4400?230:159;
        assert(!bound.target_samples);assert(!source_bound_feed(&bound,level));
        frame(level,t>=1500 && t<4400);
    }
    assert(endpoint.state==AGENT_EP_DONE && endpoint.elapsed_ms>=6000);
    reset();for(unsigned i=0;i<4;i++)frame(250,true);
    source_stream_endpoint_notice(&endpoint,records,4*320,197,AGENT_EP_TEXT);
    assert(endpoint.state==AGENT_EP_WAIT && endpoint.speech_ms==80);
    frame(250,true);assert(endpoint.state==AGENT_EP_WAIT);
    frame(250,true);assert(endpoint.state==AGENT_EP_SPEECH && endpoint.speech_ms==120);
    reset();for(unsigned i=0;i<200;i++)frame(320,true);
    assert(endpoint.state==AGENT_EP_NO_SPEECH);
    reset();for(unsigned i=0;i<20;i++)frame(320,true);
    for(unsigned i=0;i<30;i++)frame(159,false);
    assert(endpoint.state==AGENT_EP_WAIT);
    source_stream_endpoint_notice(&endpoint,records,50*320,197,AGENT_EP_TEXT);
    assert(endpoint.state==AGENT_EP_SPEECH && endpoint.elapsed_ms==1000);
    while(endpoint.state<AGENT_EP_DONE)frame(159,false);
    assert(endpoint.state==AGENT_EP_DONE && endpoint.elapsed_ms==2000);
    reset();for(unsigned i=0;i<20;i++)frame(204,true);
    for(unsigned i=0;i<8;i++)frame(140,false);
    unsigned adjusted=source_stream_endpoint_notice(&endpoint,records,28*320,197,AGENT_EP_TEXT);
    assert(adjusted==197*3/4 && endpoint.state==AGENT_EP_SPEECH && endpoint.elapsed_ms==560);
    reset();for(unsigned i=0;i<20;i++)frame(197,true);
    for(unsigned i=0;i<8;i++)frame(197,false);
    assert(source_stream_endpoint_notice(&endpoint,records,28*320,197,AGENT_EP_TEXT)==197);
    assert(endpoint.state==AGENT_EP_WAIT); // Stationary background is not made into speech.
    reset();agent_endpoint_observe(&endpoint,AGENT_EP_TEXT);
    for(unsigned i=0;i<12;i++)frame(320,true);
    assert(endpoint.state==AGENT_EP_SPEECH);
    while(endpoint.state<AGENT_EP_DONE)frame(159,false);
    assert(endpoint.state==AGENT_EP_DONE && endpoint.quiet_ms>=1600);
    reset();agent_endpoint_observe(&endpoint,AGENT_EP_TEXT);
    for(unsigned i=0;i<200;i++)frame(197,true);
    assert(endpoint.state==AGENT_EP_NO_SPEECH);
    reset();agent_endpoint_observe(&endpoint,AGENT_EP_TEXT);
    for(unsigned i=0;i<200;i++)frame(400,false);
    assert(endpoint.state==AGENT_EP_NO_SPEECH);
    reset();agent_endpoint_observe(&endpoint,AGENT_EP_TEXT);
    frame(320,true);frame(320,true);agent_endpoint_observe(&endpoint,AGENT_EP_GENERATION);
    while(endpoint.state<AGENT_EP_DONE)frame(320,true);
    assert(endpoint.state==AGENT_EP_NO_SPEECH);
    puts("P4 weak speech requires current ASR plus local spectral/energy evidence");
    return 0;
}
