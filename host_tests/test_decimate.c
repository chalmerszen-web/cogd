#include "decimate.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void dc(unsigned value)
{
    agent_decimate_t state={0}; unsigned count=0; uint16_t output=0; bool clipped=false;
    for(unsigned i=0;i<32000;++i) {
        bool ready=agent_decimate_feed(&state,(uint16_t)value,&output,&clipped);
        assert(ready==(i%2==1));
        if(ready) { ++count; assert(output==value); assert(clipped==(value<=3 || value>=4092)); }
    }
    assert(count==16000);
}

static double tone(unsigned hz)
{
    agent_decimate_t state={0}; uint16_t output; bool clipped;
    double error=0,power=0; unsigned count=0;
    for(unsigned i=0;i<32000;++i) {
        double input=2048+1500*sin(6.283185307179586*(double)hz*i/32000);
        if(!agent_decimate_feed(&state,(uint16_t)lround(input),&output,&clipped)) continue;
        assert(!clipped);
        if(i<96) continue;
        double value=(double)output-2048;
        power+=value*value; ++count;
        double ideal=1500*sin(6.283185307179586*(double)hz*((double)i-31)/32000);
        error+=(value-ideal)*(value-ideal);
    }
    double gain=sqrt(power/count)/(1500/sqrt(2));
    if(hz<=4000) { assert(gain>.995 && gain<1.005); assert(sqrt(error/count)<3); }
    else assert(gain<.004);
    return gain;
}

int main(void)
{
    const unsigned levels[]={0,3,666,2048,4092,4095};
    for(unsigned i=0;i<sizeof(levels)/sizeof(*levels);++i) dc(levels[i]);
    const unsigned frequencies[]={100,1000,3400,4000,8000,9000,12000,15000};
    for(unsigned i=0;i<sizeof(frequencies)/sizeof(*frequencies);++i)
        printf("hz=%u gain=%.9f\n",frequencies[i],tone(frequencies[i]));
    agent_decimate_t state={0},before=state; uint16_t output=123; bool clipped=false;
    assert(!agent_decimate_feed(&state,4096,&output,&clipped) && !memcmp(&state,&before,sizeof(state)));
    assert(output==123 && !clipped);
    assert(!agent_decimate_feed(NULL,0,&output,&clipped));
    assert(!agent_decimate_feed(&state,0,NULL,&clipped));
    assert(!agent_decimate_feed(&state,0,&output,NULL));
    assert(!memcmp(&state,&before,sizeof(state)));
    unsigned random=123,emitted=0;
    for(unsigned i=0;i<300000;++i) {
        random=random*1664525u+1013904223u;
        unsigned raw=i<150000?((i&1)?4095:0):random>>20;
        if(agent_decimate_feed(&state,(uint16_t)raw,&output,&clipped)) {
            ++emitted; assert(output<=4095);
            if(i<150000) assert(clipped);
        }
    }
    assert(emitted==150000);
    printf("decimator state=%zu samples=%u\n",sizeof(state),emitted);
    return 0;
}
