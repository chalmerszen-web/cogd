#include "kws_pcen.h"
#include <string.h>

/* Restoring integer square root: floor(sqrt(value)), bounded 16 iterations. */
static uint32_t root(uint32_t value)
{
    uint32_t result=0,bit=UINT32_C(1)<<30;
    while(bit>value) bit>>=2;
    while(bit) {
        if(value>=result+bit) { value-=result+bit; result=(result>>1)+bit; }
        else result>>=1;
        bit>>=2;
    }
    return result;
}

void kws_pcen_reset(kws_pcen_t *state) { memset(state,0,sizeof(*state)); }

void kws_pcen_step(kws_pcen_t *state,const uint32_t power[40],int16_t output[40])
{
    for(unsigned band=0;band<40;++band) {
        uint32_t magnitude=root(power[band]);
        uint64_t next=31*(uint64_t)state->mean_q16[band]+((uint64_t)magnitude<<16);
        uint32_t mean=(uint32_t)((next+16)>>5);
        state->mean_q16[band]=mean;
        /* Mean includes the current frame, bounding the ratio below32.
         * Widen both numerator and epsilon addition to avoid overflow.
         */
        uint32_t ratio=(uint32_t)(((uint64_t)magnitude<<28)/((uint64_t)mean+65536));
        output[band]=(int16_t)(root((ratio+8192)*16)-362);
    }
}
