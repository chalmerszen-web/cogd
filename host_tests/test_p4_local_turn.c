#include <assert.h>
#include <stdio.h>
#include <limits.h>
#include "p4_local_turn.h"
#include "p4_input_buffer.h"
#include "p4_uplink_gate.h"

static void postreply_tail_cases(void)
{
    const uint64_t capture_origin_us=199799000;
    const uint64_t output_ended_ms=224036;
    const unsigned onset=391440,detected=393360;
    p4_local_turn_t old={0},guarded={0};
    /* Candidate230 replay coordinates, with six representative admission
     * votes. They all begin inside the recorded reply-tail interval. */
    for(unsigned cursor=onset+320;cursor<=detected;cursor+=320){
        uint64_t stamp=capture_origin_us+(cursor-320)*1000000ull/16000u;
        assert(stamp>=output_ended_ms*1000+224000);
        assert(p4_uplink_reply_tail_frame(stamp,output_ended_ms));
        assert(p4_local_turn_feed(&old,cursor,true,true,1200)==
            (cursor==detected?P4_LOCAL_START:P4_LOCAL_NONE));
        assert(p4_local_turn_feed(&guarded,cursor,true,
            !p4_uplink_reply_tail_frame(stamp,output_ended_ms),1200)==P4_LOCAL_NONE);
    }
    assert(!guarded.active && !guarded.votes);
    assert(p4_uplink_reply_tail_frame(224675999,output_ended_ms));
    assert(!p4_uplink_reply_tail_frame(224676000,output_ended_ms));
    assert(!p4_uplink_reply_tail_frame(224200000,0));
    /* The post-tail follow-up still starts at six votes, and its original
     * pre-roll reaches back into the guarded time. */
    unsigned first=(224676000u-capture_origin_us)*16u/1000u;
    for(unsigned n=1;n<=6;++n){
        unsigned cursor=first+n*320;
        uint64_t stamp=capture_origin_us+(cursor-320)*1000000ull/16000u;
        assert(p4_local_turn_feed(&guarded,cursor,true,
            !p4_uplink_reply_tail_frame(stamp,output_ended_ms),1200)==
            (n==6?P4_LOCAL_START:P4_LOCAL_NONE));
    }
    assert(guarded.start<first && guarded.onset==first);
    /* The held quiet gate cannot cancel an already confirmed barge replay.
     * The recorded 1.0 cut was before reply completion; its pre-roll and
     * captured silence clock remain supplied by the duplex owner. */
    p4_local_turn_seed_replay(&guarded,222480,235008);
    assert(p4_uplink_reply_tail_frame(214523000,output_ended_ms));
    assert(p4_local_turn_feed(&guarded,235328,true,false,1200)==P4_LOCAL_NONE);
    assert(guarded.active && guarded.start==222480 && guarded.last_voice==235328);
}

static void retained_history_cases(void)
{
    const unsigned ends[]={1920,21247,21248,21249,32000,123840,UINT_MAX-320,UINT_MAX};
    unsigned controls=0;
    for(unsigned i=0;i<sizeof(ends)/sizeof(*ends);++i)for(unsigned hint=0;hint<3;++hint) {
        unsigned end=ends[i];p4_local_turn_t s={0};
        /* The sixth vote confirms at the same cursor with no hint, a recent
         * hint, or an expired hint. None changes admission or its onset. */
        for(unsigned n=6;n>0;--n) {
            unsigned at=end-(n-1)*320;
            if(hint==1)p4_local_turn_hint(&s,at,true,true);
            if(hint==2 && n==6){s.hint=true;s.hint_first=0;s.hint_last=0;}
            assert(p4_local_turn_feed(&s,at,true,true,1200)==(n==1?P4_LOCAL_START:P4_LOCAL_NONE));
        }
        assert(s.active && s.onset==end-1920 && s.last_voice==end && s.analysed==end);
        assert(s.start==(end>21248?end-21248:0));
        assert(end-s.start<=21248);
        p4_local_turn_t saved=s;
        assert(p4_local_turn_feed(&s,end,true,true,1200)==P4_LOCAL_NONE);
        assert(p4_local_turn_feed(&s,end-320,false,true,1200)==P4_LOCAL_NONE);
        assert(s.start==saved.start && s.onset==saved.onset && s.analysed==saved.analysed && s.last_voice==saved.last_voice);
        ++controls;
    }
    /* Fewer than six votes, prohibited starts and weak-only hints cannot
     * open a turn merely because more history could be retained. */
    for(unsigned mode=0;mode<3;++mode){
        p4_local_turn_t s={0};
        for(unsigned n=1;n<=300;++n){
            p4_local_turn_hint(&s,n*320,true,mode!=2);
            bool voiced=mode==0?n%2==0:mode==2;
            assert(p4_local_turn_feed(&s,n*320,voiced,mode!=2,1200)==P4_LOCAL_NONE);
        }
        assert(!s.active && !s.ended);++controls;
    }
    /* A later natural idle turn still cannot include already committed PCM. */
    for(unsigned floor=79000;floor<=81000;floor+=1000){
        p4_local_turn_t s={0};p4_local_turn_reset(&s,floor);
        for(unsigned n=1;n<=6;++n)p4_local_turn_feed(&s,100000+n*320,true,true,1200);
        assert(s.start>=(floor) && s.start==(floor>80672?floor:80672));++controls;
    }
    /* Actual180 digital-support coordinates, not a replay of unknown VAD
     * bits: representative six-vote windows match the recorded span. The
     * arithmetic is counterfactual and confers no physical acceptance. */
    const unsigned end[]={119680,123840},span[]={7,10},offset[]={56832,57344},support[]={53705,54077};
    const unsigned raw_start[]={41600,45248},lead[]={12105,8829};
    for(unsigned i=0;i<2;++i){
        p4_local_turn_t s={0};s.analysed=end[i]-320;s.votes=((1u<<(span[i]-1))|31u)>>1;
        assert(p4_local_turn_feed(&s,end[i],true,true,1200)==P4_LOCAL_START);
        assert(s.onset==end[i]-span[i]*320 && s.last_voice==end[i]);
        assert(s.start-offset[i]==raw_start[i] && support[i]-raw_start[i]==lead[i]);
        assert(s.start%320==192); /* Changed upload phase must be measured anew. */
        unsigned at=s.start+320;
        for(;at<=end[i];at+=320)assert(p4_local_turn_feed(&s,at,false,true,1200)==P4_LOCAL_NONE);
        assert(s.last_voice==end[i]);
        for(;at<end[i]+19200;at+=320)assert(p4_local_turn_feed(&s,at,false,true,1200)==P4_LOCAL_NONE);
        assert(p4_local_turn_feed(&s,at,false,true,1200)==P4_LOCAL_END);
        assert(at==end[i]+19392); /* Same 1200ms rule, next frame on phase192. */
        assert(!p4_local_turn_hold_capture(&s,at-1,false));
        assert(p4_local_turn_hold_capture(&s,at,false));
        assert(!p4_local_turn_hold_capture(&s,at,true));++controls;
    }
    /* Output interruption and ASR retries supply their own retained start;
     * they never pass through the new inactive admission branch. */
    p4_local_turn_t seeded={0};p4_local_turn_seed_replay(&seeded,102016,150016);
    for(unsigned at=102336;at<159616;at+=320)
        assert(p4_local_turn_feed(&seeded,at,false,true,600)==P4_LOCAL_NONE);
    assert(seeded.start==102016 && seeded.onset==150016 && seeded.last_voice==150016);
    assert(p4_local_turn_feed(&seeded,159616,false,true,600)==P4_LOCAL_END);
    printf("PASS bounded idle history controls=%u plus seeded replay; actual180 counterfactual only\n",controls);
}

int main(void)
{
    postreply_tail_cases();
    retained_history_cases();
    /* A weak noise hint stays stored, but cannot indefinitely suppress the
     * optional post-recovery announcement after the room becomes quiet. */
    p4_local_turn_t quiet_hint={.hint=true,.hint_last=16000};
    assert(p4_local_turn_hint_recent(&quiet_hint,16000));
    assert(p4_local_turn_hint_recent(&quiet_hint,22400));
    assert(!p4_local_turn_hint_recent(&quiet_hint,22401));
    assert(!p4_local_turn_hint_recent(&quiet_hint,15999));
    quiet_hint.hint_last=UINT_MAX-3200u;
    assert(p4_local_turn_hint_recent(&quiet_hint,1599));
    quiet_hint.hint=false;
    assert(!p4_local_turn_hint_recent(&quiet_hint,1599));
    /* Captured count-steps source has a 924 ms inter-clause pause. Keep
     * the qualifier in the same turn while preserving a short status query. */
    p4_local_turn_t clause={0};p4_local_turn_seed(&clause,0,16000);
    for(unsigned n=1;n<=47;++n)
        assert(p4_local_turn_feed(&clause,16000+n*320,false,true,p4_local_silence(1200,false))==P4_LOCAL_NONE);
    assert(p4_local_turn_feed(&clause,31360,true,true,p4_local_silence(1200,false))==P4_LOCAL_NONE);
    for(unsigned n=1;n<=60;++n)
        assert(p4_local_turn_feed(&clause,31360+n*320,false,true,p4_local_silence(1200,false))==
            (n==60?P4_LOCAL_END:P4_LOCAL_NONE));
    p4_local_turn_seed(&clause,0,16000);
    for(unsigned n=1;n<=30;++n)
        assert(p4_local_turn_feed(&clause,16000+n*320,false,true,p4_local_silence(1200,true))==
            (n==30?P4_LOCAL_END:P4_LOCAL_NONE));
    assert(p4_local_silence(400,true)==400);
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
    assert(s.active&&cursor-s.start<=21248);
    p4_local_turn_reset(&s,0);
    for(unsigned i=0;i<6;i++){
        cursor+=320;
        assert(p4_local_turn_feed(&s,cursor,true,true,600)==(i==5?P4_LOCAL_START:P4_LOCAL_NONE));
    }
    assert(s.start==cursor-21248 && s.onset==cursor-6*320);
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
    assert(s.active && s.start==10000); /* Bounded history reaches the committed floor. */
    /* A past transient cannot anchor a later utterance; replay is inert. */
    p4_local_turn_reset(&s,0);p4_local_turn_hint(&s,20000,true,true);
    p4_local_turn_feed(&s,20000,false,true,600);
    p4_local_turn_hint(&s,19900,true,true);assert(s.hint_last==20000);
    cursor=30000;
    for(unsigned i=0;i<6;i++)p4_local_turn_feed(&s,cursor+=320,true,true,600);
    assert(s.start==cursor-21248 && s.onset==30000);
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
    /* Physical candidate153: a noise turn ended at capture 613917 ms while
     * a full new phrase was already confirmed by capture at 615063 ms.
     * Replaying only the old interval then lost the real phrase's prefix. */
    p4_local_turn_seed(&s,456832,479552);
    assert(p4_local_turn_live_voice(&s,498752,613917,615086,615063));
    assert(s.last_voice==517088);
    assert(p4_local_turn_feed(&s,498752,false,true,1200)==P4_LOCAL_NONE);
    assert(p4_local_turn_feed(&s,518000,true,true,1200)==P4_LOCAL_NONE);
    assert(p4_local_turn_feed(&s,537199,false,true,1200)==P4_LOCAL_NONE);
    assert(p4_local_turn_feed(&s,537200,false,true,1200)==P4_LOCAL_END);
    assert(!p4_local_turn_live_voice(&s,537200,620000,620200,620190));
    p4_local_turn_reset(&s,0);
    assert(!p4_local_turn_live_voice(&s,10000,1000,1200,1190));
    p4_local_turn_seed(&s,0,10000);
    assert(!p4_local_turn_live_voice(&s,12000,1000,1400,1079)); /* stale */
    assert(!p4_local_turn_live_voice(&s,12000,1000,1200,1201)); /* future */
    assert(!p4_local_turn_live_voice(&s,12000,1000,1200,0));
    assert(!p4_local_turn_live_voice(&s,12000,1000,1200,1000));
    assert(!p4_local_turn_live_voice(&s,12000,1000,3100,3001)); /* bad mapping */
    assert(p4_local_turn_live_voice(&s,12000,UINT32_MAX-99u,50,40));
    assert(s.last_voice==14240); /* wrapping millisecond clock */
    /* continuous165: producer hints extended an otherwise silent candidate
     * every ~1.8 s until the provider's 30-second input limit. Each lookahead
     * must be confirmed by the analysed audio before granting another one. */
    p4_local_turn_seed(&s,0,10000);
    unsigned loans=0,ended_at=0;
    for(unsigned cursor=10320;cursor<810000;cursor+=320) {
        uint32_t capture_ms=1000+cursor/16u,now=capture_ms+1000;
        if(cursor>=s.last_voice && cursor-s.last_voice>=19200u &&
           p4_local_turn_live_voice(&s,cursor,capture_ms,now,now-20))++loans;
        if(p4_local_turn_feed(&s,cursor,false,true,1200)==P4_LOCAL_END){ended_at=cursor;break;}
    }
    assert(ended_at && ended_at<66000 && loans==1);
    /* A genuine later voiced frame confirms the borrowed interval and may
     * protect a subsequent phrase. Old replay before it cannot rearm it. */
    p4_local_turn_seed(&s,0,10000);
    assert(p4_local_turn_live_voice(&s,29200,1000,2000,1980));
    unsigned borrowed=s.last_voice;
    assert(p4_local_turn_feed(&s,borrowed-320,true,true,1200)==P4_LOCAL_NONE);
    assert(!p4_local_turn_live_voice(&s,borrowed-320,2000,3000,2980));
    assert(p4_local_turn_feed(&s,borrowed+320,true,true,1200)==P4_LOCAL_NONE);
    assert(p4_local_turn_live_voice(&s,borrowed+19520,3000,4000,3980));
    /* Candidate162 quick-13: an older empty candidate ended just before a
     * real 1.28-second question. Consuming the following frames while awaiting
     * ASR discarded its onset; retrying the old interval could not recover it.
     * Preserve every later frame across both the original commit and retry. */
    for(unsigned attempt=0;attempt<2;++attempt) {
        p4_local_turn_seed(&s,1722752,1730752);
        s.last_voice=1748032;
        assert(p4_local_turn_feed(&s,1767232,false,true,1200)==P4_LOCAL_END);
        assert(!p4_local_turn_hold_capture(&s,1766912,false)); /* endpoint BUSY */
        unsigned consumer=1767232;
        for(unsigned produced=consumer+320;produced<=1767232+20480;produced+=320) {
            assert(p4_local_turn_hold_capture(&s,consumer,false));
            /* A live producer can advance; this consumer must remain parked. */
            assert(consumer==1767232);
        }
        assert(!p4_local_turn_hold_capture(&s,consumer,true));
        p4_local_turn_reset(&s,consumer);
        assert(!p4_local_turn_hold_capture(&s,consumer,false));
        for(unsigned i=0;i<6;++i)p4_local_turn_feed(&s,consumer+=320,true,true,1200);
        assert(s.active && s.start==1767232 && s.onset==1767232);
        assert(consumer==1767232+1920); /* new question's prefix is retained */
    }
    puts("PASS local utterance onset, pause, replay, endpoint and playback gates");
}
