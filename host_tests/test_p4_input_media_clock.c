#include <assert.h>
#include <stdio.h>
#include "p4_input_media_clock.h"

int main(void)
{
    p4_input_media_clock_t s={0};
    assert(!p4_input_media_clock_wait(&s,1000));
    /* Captured failure: 160 twenty-ms frames accepted over 1.6 seconds. */
    for(unsigned i=0;i<160;++i)p4_input_media_clock_accept(&s,3297076u+i*10u,320);
    assert(s.samples==51200);
    assert(p4_input_media_clock_wait(&s,3298675)==1601);
    assert(p4_input_media_clock_wait(&s,3300275)==1);
    assert(!p4_input_media_clock_wait(&s,3300276));
    /* Ordinary realtime input needs only the final frame's duration. */
    s=(p4_input_media_clock_t){0};
    for(unsigned i=0;i<160;++i)p4_input_media_clock_accept(&s,1000+i*20,320);
    assert(p4_input_media_clock_wait(&s,4180)==20);
    assert(!p4_input_media_clock_wait(&s,4200));
    assert(!p4_input_media_clock_wait(&s,5000));
    /* No accepted samples during a BUSY wait; resetting a turn cannot carry
     * the prior turn's clock or sample count into a new question. */
    s=(p4_input_media_clock_t){0};
    p4_input_media_clock_accept(&s,6000,0);assert(!s.samples);
    p4_input_media_clock_accept(&s,7000,320);
    assert(p4_input_media_clock_wait(&s,7005)==15 && s.samples==320);
    assert(p4_input_media_clock_wait(&s,6999)==20);
    s=(p4_input_media_clock_t){0};
    p4_input_media_clock_accept(&s,8000,1);
    assert(p4_input_media_clock_wait(&s,8000)==1);
    assert(!p4_input_media_clock_wait(&s,8001));
    /* Recorded continuous-followup boundary: final accepted frame at
     * 789804 ms, commit one millisecond later despite 20 ms of new PCM.
     * Earlier control pauses must not erase the last-frame barrier. */
    s=(p4_input_media_clock_t){0};
    p4_input_media_clock_accept(&s,785000,320);
    p4_input_media_clock_accept(&s,789804,320);
    assert(p4_input_media_clock_wait(&s,789805)==19);
    assert(p4_input_media_clock_wait(&s,789823)==1);
    assert(!p4_input_media_clock_wait(&s,789824));
    puts("PASS accepted media duration before manual commit");
}
