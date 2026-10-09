#include "kws_pcen.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    kws_pcen_t state;
    uint32_t input[40]={0},random=17;
    int16_t output[40],repeated[40];
    kws_pcen_reset(&state);
    assert(sizeof(state)==160);
    kws_pcen_step(&state,input,output);
    for(unsigned j=0;j<40;++j) assert(output[j]==0);
    for(unsigned frame=0;frame<4096;++frame) {
        for(unsigned j=0;j<40;++j) {
            random=random*1664525u+1013904223u;
            input[j]=frame==0?UINT32_MAX:frame==1?0:random;
        }
        kws_pcen_step(&state,input,output);
        for(unsigned j=0;j<40;++j) assert(output[j]>=0 && output[j]<=1131);
    }
    kws_pcen_reset(&state);kws_pcen_step(&state,input,output);
    kws_pcen_reset(&state);kws_pcen_step(&state,input,repeated);
    assert(!memcmp(output,repeated,sizeof(output)));
    memset(input,0,sizeof(input));kws_pcen_reset(&state);
    for(unsigned frame=0;frame<1000;++frame) kws_pcen_step(&state,input,output);
    for(unsigned j=0;j<40;++j) assert(output[j]==0 && state.mean_q16[j]==0);
    puts("PCEN160Bstate; silence/reset/4096full-rangeframes passed");
    return 0;
}
