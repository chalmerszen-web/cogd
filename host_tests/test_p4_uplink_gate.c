#include "p4_uplink_gate.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* A response delivered late must not erase the user's queued tail. */
    assert(!p4_uplink_reply_frame(800000,1100));
    assert(!p4_uplink_reply_frame(1080000,1100));
    assert(p4_uplink_reply_frame(1090000,1100));
    assert(!p4_uplink_reply_frame(1100000,0));
    assert(!p4_uplink_cue_frame(980,1000,1180));
    assert(p4_uplink_cue_frame(990,1000,1180));
    assert(p4_uplink_cue_frame(1100,1000,1180));
    assert(p4_uplink_cue_frame(1403,1000,1180));
    assert(!p4_uplink_cue_frame(1404,1000,1180));
    /* Cancelled/incomplete and old cues cannot silence future input. */
    assert(p4_uplink_cue_frame(1200,1000,0));
    assert(!p4_uplink_cue_frame(1800,1000,0));
    assert(!p4_uplink_cue_frame(9000,1000,1180));
    assert(!p4_uplink_cue_frame(1000,0,0));
    /* Cue timestamps wrap every 49 days. */
    assert(p4_uplink_cue_frame(25,UINT32_MAX-99,80));
    assert(p4_uplink_cue_frame(303,UINT32_MAX-99,80));
    assert(!p4_uplink_cue_frame(304,UINT32_MAX-99,80));
    puts("P4 capture-clock cue/reply gate: PASS");
    return 0;
}
