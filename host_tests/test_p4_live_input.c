#include <assert.h>
#include <stdio.h>
#include "p4_live_input.h"
#include "p4_local_turn.h"
#include "p4_input_media_clock.h"
int main(void)
{
    /* Do not turn stale startup audio into a permanent real-time backlog. */
    assert(!p4_live_input_start(true,false,false,1000000,1080001));
    assert(p4_live_input_start(true,false,false,1000000,1080000));
    assert(!p4_live_input_start(true,false,false,1000001,1000000));
    assert(!p4_live_input_start(false,false,false,1000000,1000000));
    assert(!p4_live_input_start(true,true,false,1000000,1000000));
    assert(!p4_live_input_start(true,false,true,1000000,1000000));
    p4_live_input_t live={0};p4_local_turn_t turn={0};p4_input_media_clock_t clock={0};
    unsigned starts=0,ends=0,uploaded=0;
    /* Two seconds of live silence, a three-second utterance and its 600 ms
     * endpoint: no duplicate prefix and no extra post-utterance replay wait. */
    for(unsigned f=0;f<280;++f){
        unsigned cursor=(f+1)*320;
        p4_local_event_t e=p4_local_turn_feed(&turn,cursor,f>=100&&f<250,true,600);
        if(e==P4_LOCAL_START){++starts;assert(p4_live_input_contains(&live,turn.onset));}
        if(e==P4_LOCAL_END)++ends;
        assert(p4_live_input_accept(&live,cursor,320));uploaded+=320;
        p4_input_media_clock_accept(&clock,1000+f*20,320);
        if(e==P4_LOCAL_END)assert(p4_input_media_clock_wait(&clock,1000+f*20)<=20);
    }
    assert(starts==1&&ends==1&&uploaded==89600);
    assert(!p4_live_input_accept(&live,live.end,320)); // Duplicate/rewound PCM.
    assert(!p4_live_input_accept(&live,live.end+640,320)); // Missing frame.
    assert(p4_live_input_accept(&live,live.end+320,320));
    live=(p4_live_input_t){0}; // Acknowledged buffer clear resets the timeline.
    assert(p4_live_input_accept(&live,200000,320));
    assert(!p4_live_input_contains(&live,199000));
    /* A cached reply, masked cue and its 224 ms echo tail must preserve the
     * same live capture clock before the next utterance. Only a commit or an
     * acknowledged clear may close/reset that clock. */
    live=(p4_live_input_t){0};
    assert(!p4_live_input_continue(&live,false,true));
    assert(p4_live_input_accept(&live,320,320));
    for(unsigned f=1;f<80;++f){
        assert(p4_live_input_continue(&live,false,true));
        assert(p4_live_input_accept(&live,(f+1)*320,320));
    }
    assert(!p4_live_input_continue(&live,true,true));
    assert(!p4_live_input_continue(&live,false,false));
    puts("live microphone admission and endpoint regression passed");
}
