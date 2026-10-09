#include "source_stream.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double response(unsigned hz)
{
    agent_biquad_t state={0};double input=0,output=0;
    for(unsigned i=0;i<32000;++i) {
        int16_t x=(int16_t)(12000*sin(6.283185307179586*hz*i/16000));
        int16_t y=tonal_highpass_sample(&state,x);
        if(i>=16000) {input+=(double)x*x;output+=(double)y*y;}
    }
    return sqrt(output/input);
}
typedef struct { uint32_t crc; unsigned calls; } observed_t;
static bool spectral(void *ctx,int16_t *pcm)
{
    observed_t *o=ctx;++o->calls;
    for(unsigned i=0;i<320;++i)o->crc=o->crc*33u+(uint16_t)pcm[i];
    return true;
}
static void pipeline(void)
{
    static uint8_t records[2][SOURCE_METADATA_BYTES];
    int16_t pcm[2][320];source_stream_t stream[2];observed_t seen[2]={{0}};
    for(unsigned i=0;i<2;++i)
        assert(!source_stream_init(&stream[i],records[i],sizeof(records[i]),pcm[i],100,spectral,&seen[i]));
    stream[1].fast_guard=true;
    /* The same low-frequency disturbance reaches both classifier callbacks.
     * Only the independent clean-energy veto changes; no raw source is lost. */
    for(unsigned i=0;i<48000;++i) {
        int16_t value=(int16_t)(3000*sin(6.283185307179586*160*i/16000));
        for(unsigned j=0;j<2;++j)assert(!source_stream_feed(&stream[j],value));
    }
    assert(seen[0].calls==seen[1].calls && seen[0].crc==seen[1].crc);
    assert(stream[0].samples==stream[1].samples && stream[0].frames==stream[1].frames);
    unsigned before=0,after=0;
    for(unsigned i=50;i<150;++i) {
        source_frame_t a,b;
        assert(!source_stream_read(records[0],sizeof(records[0]),48000,i,&a));
        assert(!source_stream_read(records[1],sizeof(records[1]),48000,i,&b));
        assert(a.level==b.level && a.spectral==b.spectral);
        before+=a.clean_level;after+=b.clean_level;
    }
    assert(after<before/2);
    /* New captures cannot inherit a filter tail or accidentally enable fast
     * filtering in classic mode. */
    assert(!source_stream_init(&stream[1],records[1],sizeof(records[1]),pcm[1],100,spectral,&seen[1]));
    agent_biquad_t zero={0};assert(!stream[1].fast_guard);
    assert(!memcmp(&stream[1].energy_highpass,&zero,sizeof(zero)));
}
int main(void)
{
    assert(response(50)<.03 && response(160)<.29);
    assert(response(300)>.69 && response(300)<.72);
    assert(response(1000)>.99 && response(3000)>.99);
    agent_biquad_t state={0};uint32_t seed=0x91e10da5;
    for(unsigned i=0;i<160000;++i) {
        seed=seed*1664525u+1013904223u;
        (void)tonal_highpass_sample(&state,(int16_t)(seed>>16));
    }
    for(unsigned i=0;i<16000;++i)(void)tonal_highpass_sample(&state,0);
    assert(abs(tonal_highpass_sample(&state,0))<=1);
    pipeline();puts("Fast endpoint energy: frequency response, bounded arithmetic, classifier identity and reset PASS");
    return 0;
}
