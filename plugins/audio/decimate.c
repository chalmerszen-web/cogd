#include "decimate.h"

/* Symmetric 63-tap low-pass, 32 kHz, 6 kHz cutoff, Kaiser beta=5, Q15.
 * The center corrects rounding so DC gain is exactly one. Passband 0..4 kHz
 * deviates <0.01 dB; stopband 8..16 kHz is below -64 dB. Delay: 31/32000 s.
 * Sum(abs(taps))=61540: the full 12-bit input bound is 252006300 < INT32_MAX. */
static const int16_t taps[32]={
    -11,-13,10,35,17,-41,-67,0,100,93,-60,-185,-84,181,275,0,
    -370,-327,204,615,272,-582,-884,0,1227,1130,-753,-2513,-1303,3655,9617,12292
};

bool agent_decimate_feed(agent_decimate_t *s,uint16_t input,uint16_t *output,bool *clipped)
{
    if(!s || !output || !clipped || input>4095) return false;
    if(!s->initialized) {
        /* A DC-biased ADC must not acquire a synthetic startup step from zero. */
        for(unsigned i=0;i<64;++i) s->history[i]=input;
        s->initialized=true;
    }
    unsigned recent=s->at;
    s->history[recent]=input; s->at=(recent+1u)&63u;
    s->rails|=input<=3 || input>=4092;
    if(++s->phase<2) return false;
    s->phase=0;
    int32_t sum=(int32_t)s->history[(recent-31u)&63u]*taps[31];
    for(unsigned i=0;i<31;++i) {
        unsigned pair=(unsigned)s->history[(recent-i)&63u]+s->history[(recent-62u+i)&63u];
        sum+=(int32_t)pair*taps[i];
    }
    int32_t value=sum>=0?(sum+16384)/32768:-((-sum+16384)/32768);
    *clipped=s->rails || value<0 || value>4095; s->rails=false;
    *output=(uint16_t)(value<0?0:value>4095?4095:value);
    return true;
}
