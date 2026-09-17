#include "crc.h"
#include <assert.h>
#include <stdio.h>

static uint32_t reference(uint32_t state,const uint8_t *data,size_t size)
{
    while(size--) {
        state^=*data++;
        for(unsigned bit=0;bit<8;++bit)
            state=(state>>1)^((state&1u)?0xedb88320u:0u);
    }
    return state;
}
static uint32_t random_value(uint32_t *state)
{ *state=*state*1664525u+1013904223u;return *state; }

int main(void)
{
    assert(agent_crc32(NULL,0)==0);
    assert(agent_crc32("123456789",9)==0xcbf43926u);
    uint8_t data[4103];uint32_t seed=7;size_t total=0;
    for(unsigned round=0;round<4096;++round) {
        for(size_t i=0;i<sizeof(data);++i) data[i]=(uint8_t)(random_value(&seed)>>24);
        size_t skip=round%8,n=round;uint32_t start=random_value(&seed);
        uint32_t expected=reference(start,data+skip,n);
        assert(agent_crc32_update(start,data+skip,n)==expected);
        assert(agent_crc32_update(start,NULL,0)==start);
        uint32_t streamed=start;
        for(size_t at=0;at<n;) {
            size_t count=1+random_value(&seed)%257;if(count>n-at)count=n-at;
            streamed=agent_crc32_update(streamed,data+skip+at,count);at+=count;
        }
        assert(streamed==expected);total+=n;
    }
    printf("CRC reference bytes=%zu, arbitrary initial states, unaligned inputs and random chunks exact\n",total);
    return 0;
}
