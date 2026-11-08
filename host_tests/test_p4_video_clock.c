#include "p4_video_clock.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    p4_video_pending_t frame={23000,7,10000000};
    /* Real upload lag was 1.6--3.7 s. Retaining this capture lets audio catch
     * it despite newer camera frames; neither its identity nor clock changes. */
    for(uint64_t now=10000000;now<12500000;now+=20000){
        assert(!p4_video_pending_due(&frame,now-2500000));
        assert(!p4_video_pending_expired(&frame,now));
    }
    assert(p4_video_pending_due(&frame,10000000));
    assert(frame.stamp==10000000 && frame.sequence==7 && frame.bytes==23000);
    assert(!p4_video_pending_expired(&frame,15000000));
    assert(p4_video_pending_expired(&frame,15000001));
    frame=(p4_video_pending_t){0};
    assert(!p4_video_pending_due(&frame,20000000));
    assert(!p4_video_pending_expired(&frame,20000000));
    puts("video capture clock retention passed");return 0;
}
