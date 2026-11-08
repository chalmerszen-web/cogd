#ifndef COGD_P4_INPUT_MEDIA_CLOCK_H
#define COGD_P4_INPUT_MEDIA_CLOCK_H
#include <stdint.h>

/* A successful UDP send is not a media-consumption acknowledgement. A real
 * 3.2-second interruption uploaded in 1.6 seconds remained complete locally
 * but was only partly transcribed when commit immediately followed it.
 * Keep manual commit behind the accepted 16 kHz media duration. This is a
 * conservative local clock barrier, not proof of server receipt/consumption.
 * Only successfully accepted PCM advances it; BUSY retries never do. */
typedef struct {uint64_t first_ms;uint32_t samples;} p4_input_media_clock_t;
static inline void p4_input_media_clock_accept(p4_input_media_clock_t *s,uint64_t now,uint32_t samples)
{
    if(!samples)return;
    if(!s->samples)s->first_ms=now;
    s->samples+=samples; /* Caller limits an utterance to 60 seconds. */
}
static inline uint32_t p4_input_media_clock_wait(const p4_input_media_clock_t *s,uint64_t now)
{
    uint64_t duration=((uint64_t)s->samples+15)/16;
    uint64_t elapsed=now>=s->first_ms?now-s->first_ms:0;
    return elapsed>=duration?0:(uint32_t)(duration-elapsed);
}
#endif
