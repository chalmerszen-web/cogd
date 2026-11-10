#include <assert.h>
#include <stdio.h>
#include "p4_local_turn.h"
#include "p4_input_buffer.h"

int main(void)
{
    assert(p4_local_onset_level(0)==280);
    assert(p4_local_onset_level(140)==280);
    assert(p4_local_onset_level(480)==960);
    /* Captured slow progress query: idle floor 232, post-phrase input
     * 226..296 kept refreshing the old active gate for several seconds. */
    assert(p4_local_continuation_level(232)==348);
    assert(p4_local_continuation_level(0)==140);
    p4_local_turn_t noise_tail={0};p4_local_turn_seed(&noise_tail,0,16000);
    for(unsigned i=1;i<=30;i++) {
        unsigned input_level=226+(i%8)*10;
        bool voiced=input_level>p4_local_continuation_level(232);
        assert(p4_local_turn_feed(&noise_tail,16000+i*320,voiced,true,600)==
            (i==30?P4_LOCAL_END:P4_LOCAL_NONE));
    }
    /* Softer sustained continuation remains above the lower active gate;
     * it need not meet the original 464 admission level again. */
    p4_local_turn_seed(&noise_tail,0,16000);
    for(unsigned i=1;i<=60;i++)
        assert(p4_local_turn_feed(&noise_tail,16000+i*320,
            360>p4_local_continuation_level(232),true,600)==P4_LOCAL_NONE);
    assert(noise_tail.last_voice==35200 && !noise_tail.ended);
    p4_local_turn_t s={0};unsigned cursor=0;
    /* Sparse acoustic pulses never become a user turn. */
    for(unsigned i=0;i<100;i++){
        cursor+=320;
        assert(p4_local_turn_feed(&s,cursor,i%5==0,true,600)==P4_LOCAL_NONE);
    }
    for(unsigned i=0;i<6 && !s.active;i++)p4_local_turn_feed(&s,cursor+=320,true,true,600);
    assert(s.active&&cursor-s.start<=11200);
    p4_local_turn_reset(&s,0);
    for(unsigned i=0;i<6;i++){
        cursor+=320;
        assert(p4_local_turn_feed(&s,cursor,true,true,600)==(i==5?P4_LOCAL_START:P4_LOCAL_NONE));
    }
    assert(s.start==cursor-6*320-8000);
    unsigned detected=cursor;
    /* Pre-roll/transport retries are not fresh silence. */
    for(unsigned at=s.start+320;at<=detected;at+=320)
        assert(p4_local_turn_feed(&s,at,false,true,600)==P4_LOCAL_NONE);
    assert(s.last_voice==detected);
    for(unsigned i=0;i<20;i++){
        cursor+=320;assert(p4_local_turn_feed(&s,cursor,false,true,600)==P4_LOCAL_NONE);
    }
    cursor+=320;assert(p4_local_turn_feed(&s,cursor,true,true,600)==P4_LOCAL_NONE);
    /* A 400 ms pause followed by the rest remains one utterance. */
    for(unsigned i=0;i<30;i++){
        cursor+=320;
        assert(p4_local_turn_feed(&s,cursor,false,true,600)==(i==29?P4_LOCAL_END:P4_LOCAL_NONE));
    }
    assert(s.ended);
    assert(p4_local_turn_feed(&s,cursor+320,false,true,600)==P4_LOCAL_NONE);
    /* A replay seeded by confirmed barge-in cannot end in its own pre-roll. */
    p4_local_turn_seed(&s,10000,18000);
    for(unsigned at=10320;at<27600;at+=320)
        assert(p4_local_turn_feed(&s,at,false,true,600)==P4_LOCAL_NONE);
    assert(p4_local_turn_feed(&s,27600,false,true,600)==P4_LOCAL_END);
    /* Candidate 95 captured regression: replay started at 102016 but the
     * confirmed microphone cursor was 150016. The old fixed 500 ms seed
     * committed at 119616, before the actual question reached the uplink. */
    p4_local_turn_seed(&s,102016,110016);
    for(unsigned at=102336;at<119616;at+=320)
        assert(p4_local_turn_feed(&s,at,false,true,600)==P4_LOCAL_NONE);
    assert(p4_local_turn_feed(&s,119616,false,true,600)==P4_LOCAL_END);
    p4_local_turn_seed_replay(&s,102016,150016);
    for(unsigned at=102336;at<159616;at+=320)
        assert(p4_local_turn_feed(&s,at,false,true,600)==P4_LOCAL_NONE);
    assert(p4_local_turn_feed(&s,159616,false,true,600)==P4_LOCAL_END);
    p4_local_turn_seed_replay(&s,102016,150016);
    assert(p4_local_turn_feed(&s,155136,true,true,600)==P4_LOCAL_NONE);
    assert(p4_local_turn_feed(&s,164416,false,true,600)==P4_LOCAL_NONE);
    assert(p4_local_turn_feed(&s,164736,false,true,600)==P4_LOCAL_END);
    p4_local_turn_seed_replay(&s,102016,0);assert(s.last_voice==110016);
    p4_local_turn_seed_replay(&s,102016,150017);assert(s.last_voice==110016);
    /* A new turn must not re-upload already committed media timestamps. */
    p4_local_turn_reset(&s,50000);cursor=50000;
    for(unsigned i=0;i<6;i++)p4_local_turn_feed(&s,cursor+=320,true,true,600);
    assert(s.active&&s.start==50000);
    /* Playback/cue suppression discards onset votes, not the capture clock. */
    p4_local_turn_reset(&s,0);
    for(unsigned i=0;i<10;i++)assert(p4_local_turn_feed(&s,320*(i+1),true,false,600)==P4_LOCAL_NONE);
    assert(!s.active&&!s.votes);
    /* Quiet leading syllables are retained without lowering admission.
     * 480 ms of weak voice precedes the six strong frames. */
    p4_local_turn_reset(&s,10000);cursor=20000;
    for(unsigned i=0;i<24;i++) {
        cursor+=320;p4_local_turn_hint(&s,cursor,true,true);
        assert(p4_local_turn_feed(&s,cursor,false,true,600)==P4_LOCAL_NONE);
    }
    assert(!s.active);
    for(unsigned i=0;i<6;i++) {
        cursor+=320;p4_local_turn_hint(&s,cursor,true,true);
        p4_local_turn_feed(&s,cursor,true,true,600);
    }
    assert(s.active && s.start==20000-2048);
    /* A past transient cannot anchor a later utterance; replay is inert. */
    p4_local_turn_reset(&s,0);p4_local_turn_hint(&s,20000,true,true);
    p4_local_turn_feed(&s,20000,false,true,600);
    p4_local_turn_hint(&s,19900,true,true);assert(s.hint_last==20000);
    cursor=30000;
    for(unsigned i=0;i<6;i++)p4_local_turn_feed(&s,cursor+=320,true,true,600);
    assert(s.start==30000-8000);
    /* A continuous noise hint stays bounded and never starts a turn. */
    p4_local_turn_reset(&s,0);
    for(cursor=320;cursor<96000;cursor+=320) {
        p4_local_turn_hint(&s,cursor,true,true);
        assert(p4_local_turn_feed(&s,cursor,false,true,600)==P4_LOCAL_NONE);
        assert(cursor-s.hint_first<=19200);
    }
    p4_local_turn_hint(&s,cursor,true,false);assert(!s.hint);
    p4_local_turn_reset(&s,50000);cursor=50000;
    for(unsigned i=0;i<6;i++) {
        cursor+=320;p4_local_turn_hint(&s,cursor,true,true);
        p4_local_turn_feed(&s,cursor,true,true,600);
    }
    assert(s.start==50000);
    /* A commit is not proof of speech. Empty ASR never spends a response. */
    assert(p4_local_reply(false,true,true,false)==P4_LOCAL_WAIT);
    assert(p4_local_reply(true,false,false,false)==P4_LOCAL_WAIT);
    assert(p4_local_reply(true,true,false,false)==P4_LOCAL_REJECT);
    assert(p4_local_reply(true,true,true,false)==P4_LOCAL_RESPOND);
    assert(p4_local_reply(true,true,true,true)==P4_LOCAL_WAIT);
    /* Reproduce long-playout input timeout without ever clearing new speech. */
    p4_input_buffer_t buffer={0};
    assert(!p4_input_buffer_due(&buffer,60000,false));
    p4_input_buffer_committed(&buffer,1000);
    assert(!p4_input_buffer_due(&buffer,20999,false));
    assert(!p4_input_buffer_due(&buffer,21000,true));
    assert(p4_input_buffer_due(&buffer,21000,false));
    p4_input_buffer_sent(&buffer,21000);
    assert(!p4_input_buffer_due(&buffer,50000,false));
    assert(!p4_input_buffer_timeout(&buffer,23999));
    assert(p4_input_buffer_timeout(&buffer,24000));
    assert(p4_input_buffer_cleared(&buffer,21200));
    assert(!p4_input_buffer_cleared(&buffer,21200));
    assert(!p4_input_buffer_due(&buffer,41199,false));
    assert(p4_input_buffer_due(&buffer,41200,false));
    p4_input_buffer_committed(&buffer,42000);
    assert(!p4_input_buffer_due(&buffer,50000,false));
    buffer.full=true;
    assert(!p4_input_buffer_due(&buffer,50000,true)); /* output or new input */
    assert(p4_input_buffer_due(&buffer,50000,false));
    p4_input_buffer_sent(&buffer,50000);
    assert(p4_input_buffer_cleared(&buffer,50100) && !buffer.full);
    puts("PASS local utterance onset, pause, replay, endpoint and playback gates");
}
