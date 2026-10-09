/* Host-only compiler comparison. Uses actual producer sources; no hardware. */
#define _POSIX_C_SOURCE 200809L
#include "source_stream.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint32_t hash=2166136261u,random_state=1;
static void digest(uint32_t value)
{ hash=(hash^value)*16777619u; }
static uint32_t random_u32(void)
{ random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state; }
static bool spectral(void *unused,int16_t *pcm)
{
    (void)unused;
    /* Deterministic substitute isolates producer cost; no WebRTC is linked. */
    int32_t crossings=0;
    for(unsigned i=1;i<320;++i)crossings+=(pcm[i]<0)!=(pcm[i-1]<0);
    return crossings>=8 && crossings<=180;
}
static int16_t sample(unsigned kind,unsigned i)
{
    switch(kind) {
    case 0:return (int16_t)((int32_t)(random_u32()&65535u)-32768);
    case 1:return (int16_t)((int32_t)(random_u32()&2047u)-1024);
    case 2:return i%16000<8000?32767:-32768;
    case 3:return i%16000==0?32767:0;
    default:return i&1?32767:-32768;
    }
}
static void state(const agent_biquad_t *s)
{ digest((uint32_t)s->x1);digest((uint32_t)s->x2);digest((uint32_t)s->y1);digest((uint32_t)s->y2); }
int main(int argc,char **argv)
{
    bool metadata=argc>1 && argv[1][0]=='s';
    unsigned repeats=argc>2?(unsigned)strtoul(argv[2],NULL,10):8;
    if(!repeats || repeats>100)return 2;
    struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
    uint64_t samples=0;
    for(unsigned run=0;run<repeats;++run)for(unsigned kind=0;kind<5;++kind) {
        source_stream_t stream;
        uint8_t records[SOURCE_METADATA_BYTES];int16_t filtered[SOURCE_FRAME_SAMPLES];
        assert(!source_stream_init(&stream,records,sizeof(records),filtered,0,spectral,NULL));
        for(unsigned i=0;i<160000;++i) {
            int16_t value=sample(kind,i);
            if(metadata) {
                if(source_stream_done(&stream))break;
                assert(!source_stream_feed(&stream,value));
            } else {
                value=agent_voice_reject_cue(&stream.notch,agent_voice_filter(&stream.voice,value));
                digest((uint16_t)tonal_filter_sample(&stream.tonal,value));
            }
            ++samples;
        }
        for(unsigned i=0;i<4;++i)state(stream.voice.stage+i);
        state(&stream.notch);for(unsigned i=0;i<2;++i)state(stream.tonal.tones+i);
        if(metadata) {
            for(unsigned i=0;i<sizeof(records);++i)digest(records[i]);
            for(unsigned i=0;i<SOURCE_FRAME_SAMPLES;++i)digest((uint16_t)filtered[i]);
            digest(stream.samples);digest(stream.bound.target_samples);
        }
    }
    clock_gettime(CLOCK_MONOTONIC,&end);
    double seconds=(double)(end.tv_sec-begin.tv_sec)+(double)(end.tv_nsec-begin.tv_nsec)/1e9;
    printf("{\"mode\":\"%s\",\"samples\":%" PRIu64 ",\"digest\":\"%08" PRIx32 "\",\"seconds\":%.9f,\"ns_per_sample\":%.3f}\n",
        metadata?"source_metadata":"seven_biquads",samples,hash,seconds,seconds*1e9/(double)samples);
    return 0;
}
