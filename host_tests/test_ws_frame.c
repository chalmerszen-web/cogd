#include "ws_frame.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t state=0x7b65fe12u;
static uint32_t random_word(void)
{ state^=state<<13;state^=state>>17;state^=state<<5;return state; }

static void roundtrip(size_t length,bool binary)
{
    uint8_t payload[AGENT_WS_FRAME_PAYLOAD_MAX],original[AGENT_WS_FRAME_PAYLOAD_MAX];
    uint8_t mask[4],guarded[AGENT_WS_FRAME_MAX+2];
    for(size_t i=0;i<length;++i)payload[i]=(uint8_t)random_word();
    memcpy(original,payload,length);
    for(unsigned i=0;i<4;++i)mask[i]=(uint8_t)random_word();
    memset(guarded,0xa5,sizeof(guarded));
    uint8_t *frame=guarded+1;
    size_t header=length<=125?6:8,total=header+length;
    assert(agent_ws_frame_pack(frame,total,binary,payload,length,mask)==total);
    assert(guarded[0]==0xa5 && guarded[total+1]==0xa5);
    assert(frame[0]==(binary?0x82:0x81));
    assert(frame[1]&0x80);
    if(length<=125)assert((frame[1]&0x7f)==length);
    else {
        assert((frame[1]&0x7f)==126);
        assert(((size_t)frame[2]<<8 | frame[3])==length);
    }
    assert(!memcmp(frame+header-4,mask,sizeof(mask)));
    for(size_t i=0;i<length;++i)assert((frame[header+i]^mask[i&3])==payload[i]);
    assert(!memcmp(original,payload,length));
    memset(guarded,0xa5,sizeof(guarded));
    assert(!agent_ws_frame_pack(frame,total-1,binary,payload,length,mask));
    for(size_t i=0;i<sizeof(guarded);++i)assert(guarded[i]==0xa5);
}

int main(void)
{
    const size_t boundary[]={0,1,124,125,126,127,255,256,511,512,1024,2047,2048};
    for(size_t i=0;i<sizeof(boundary)/sizeof(*boundary);++i) {
        roundtrip(boundary[i],false);roundtrip(boundary[i],true);
    }
    for(unsigned i=0;i<10000;++i)roundtrip(random_word()%2049,(random_word()&1)!=0);
    uint8_t frame[AGENT_WS_FRAME_MAX],mask[4]={0,0,0,0};
    assert(agent_ws_frame_pack(frame,sizeof(frame),false,NULL,0,mask)==6);
    assert(!agent_ws_frame_pack(frame,sizeof(frame),false,NULL,1,mask));
    assert(!agent_ws_frame_pack(NULL,sizeof(frame),false,"x",1,mask));
    assert(!agent_ws_frame_pack(frame,sizeof(frame),false,"x",1,NULL));
    assert(!agent_ws_frame_pack(frame,sizeof(frame),false,"x",2049,mask));
    puts("ws_frame: boundaries and 10000 randomized masked frames passed");
    return 0;
}
