#ifndef COGD_P4_VAD_STREAM_H
#define COGD_P4_VAD_STREAM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ESP-SR accepts 30 ms / 480 samples at 16 kHz, while AEC emits 512.
 * Retain the fractional frame: passing only the first 480 of each AEC block
 * drops 6.25 percent of the detector's signal and breaks its continuity. */
enum { P4_VAD_FRAME_SAMPLES=480 };
typedef struct {int16_t pcm[P4_VAD_FRAME_SAMPLES];unsigned used;} p4_vad_stream_t;
static inline bool p4_vad_stream_feed(p4_vad_stream_t *s,const int16_t *pcm,size_t count,
    bool (*detect)(void *,int16_t *),void *context)
{
    bool voiced=false;
    while(count){
        size_t n=P4_VAD_FRAME_SAMPLES-s->used;if(n>count)n=count;
        memcpy(s->pcm+s->used,pcm,n*sizeof(*pcm));s->used+=(unsigned)n;pcm+=n;count-=n;
        if(s->used==P4_VAD_FRAME_SAMPLES){
            bool frame=detect(context,s->pcm);voiced=voiced || frame;s->used=0;
        }
    }
    return voiced;
}
#endif
