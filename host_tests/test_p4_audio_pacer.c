#include <assert.h>
#include <stdio.h>
#include "p4_audio_pacer.h"

int main(void)
{
    /* A six-second phrase with two ms of processing per 20 ms audio frame.
     * Completion-relative scheduling used to add 600 ms of avoidable lag. */
    uint64_t due=1000,old_due=1000,finished=0,old_finished=0;
    for(unsigned i=0;i<300;i++) {
        finished=due+2;old_finished=old_due+2;
        due=p4_audio_pacer_next(due,finished,20);old_due=old_finished+20;
    }
    assert(finished==6982 && old_finished==7580);
    /* An owner stalled for 250 ms sends one frame, then waits a full period.
     * Moderate lateness is bounded to at least ten ms, never a catch-up burst. */
    assert(p4_audio_pacer_next(1000,1250,20)==1270);
    assert(p4_audio_pacer_next(1000,1018,20)==1028);
    assert(p4_audio_pacer_next(0,1000,20)==1020);
    assert(p4_audio_pacer_next(1000,999,20)==1019);
    /* Retained explicit diagnostic cadences remain bounded as well. */
    assert(p4_audio_pacer_next(1000,1001,5)==1005);
    assert(p4_audio_pacer_next(1000,1004,5)==1007);
    assert(p4_audio_pacer_next(1000,1010,10)==1020);
    puts("P4 audio pacing regression passed");
    return 0;
}
