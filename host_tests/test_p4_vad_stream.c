#include "p4_vad_stream.h"
#include <assert.h>

typedef struct {unsigned samples,frames,speech_frame;} check_t;
static int16_t sample(unsigned at){return (int16_t)(at%30000-15000);}
static bool check(void *context,int16_t *pcm)
{
    check_t *c=context;
    for(unsigned i=0;i<P4_VAD_FRAME_SAMPLES;++i)assert(pcm[i]==sample(c->samples++));
    return ++c->frames==c->speech_frame;
}
int main(void)
{
    p4_vad_stream_t stream={0};check_t c={.speech_frame=16};int16_t pcm[512];
    /* Fifteen actual AEC blocks must deliver sixteen contiguous VAD frames.
     * The former first-480-only call loses a complete 30-ms frame here. */
    for(unsigned block=0;block<15;++block){
        for(unsigned i=0;i<512;++i)pcm[i]=sample(block*512+i);
        assert(p4_vad_stream_feed(&stream,pcm,512,check,&c)==(block==14));
    }
    assert(c.samples==7680 && c.frames==16 && !stream.used);
    /* Fractional samples cross arbitrary producer boundaries without repeats. */
    stream=(p4_vad_stream_t){0};c=(check_t){0};unsigned at=0;
    const unsigned sizes[]={1,479,511,2,127,321,479};
    for(unsigned part=0;part<sizeof(sizes)/sizeof(sizes[0]);++part){
        for(unsigned i=0;i<sizes[part];++i)pcm[i]=sample(at+i);
        assert(!p4_vad_stream_feed(&stream,pcm,sizes[part],check,&c));at+=sizes[part];
        assert(c.samples+stream.used==at);
    }
    assert(!p4_vad_stream_feed(&stream,NULL,0,check,&c));
    /* A new monitor epoch drops its partial frame, not the next epoch's head. */
    stream.used=0;c=(check_t){0};
    for(unsigned i=0;i<512;++i)pcm[i]=sample(i);
    assert(!p4_vad_stream_feed(&stream,pcm,512,check,&c));
    assert(c.samples==480 && stream.used==32);
    return 0;
}
