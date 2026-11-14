#include "p4_asr_retry.h"
#include "p4_idle_voice_evidence.h"
#include <assert.h>
#include <limits.h>

static unsigned reader,producer,rewinds;

static bool rewind_capture(unsigned start)
{
    ++rewinds;
    if(producer-start>176000u)return false; /* 12 s ring minus 1 s reserve */
    reader=start;
    return true;
}

static void readiness_regression(void)
{
    p4_asr_retry_t s={0};
    assert(p4_asr_retry_begin(&s,1182736,1218256,1237456));
    reader=s.end;
    /* Numeric event cursors are from candidate230's last empty turn. The
     * actual producer cursor was not logged. Model a ring where rewinding
     * before setup exhausts headroom, but the forward reader is still safe. */
    producer=s.start+171000;
    assert(rewind_capture(s.start));
    producer+=32000;
    assert(producer-reader>192000u);
    reader=s.end;
    producer=s.start+171000;
    bool waiting=true;
    unsigned attempts=rewinds;
    assert(p4_asr_retry_replay(&s,&waiting,false,rewind_capture)==P4_ASR_REPLAY_WAIT);
    assert(waiting && reader==s.end && rewinds==attempts);
    producer+=32000;
    assert(producer-reader<192000u);
    assert(p4_asr_retry_replay(&s,&waiting,true,rewind_capture)==P4_ASR_REPLAY_EXPIRED);
    assert(!waiting && reader==s.end && rewinds==attempts+1);
    assert(p4_asr_retry_replay(&s,&waiting,true,rewind_capture)==P4_ASR_REPLAY_WAIT);
    assert(reader==s.end && rewinds==attempts+1);
    /* A retained retry still replays after its ACKs, exactly once. */
    producer=s.start+176000;
    waiting=true;
    assert(p4_asr_retry_replay(&s,&waiting,false,rewind_capture)==P4_ASR_REPLAY_WAIT);
    assert(p4_asr_retry_replay(&s,&waiting,true,rewind_capture)==P4_ASR_REPLAY_READY);
    assert(!waiting && reader==s.start && rewinds==attempts+2);
}

static p4_idle_voice_evidence_t quiet_window(unsigned eligible,unsigned rough,unsigned structure,bool missing,
    unsigned rough_weight,unsigned smooth_weight)
{
    p4_idle_voice_window_t window={.from_ms=1000,.through_ms=1384};
    assert(p4_idle_voice_frame(&window,1416,P4_IDLE_VOICE_FRAME_STRUCTURE));
    for(unsigned i=0;i<13;++i){
        unsigned flags=P4_IDLE_VOICE_FRAME_VALID;
        if(i<eligible)flags|=P4_IDLE_VOICE_FRAME_ELIGIBLE;
        if(i<rough)flags|=P4_IDLE_VOICE_FRAME_ROUGH;
        if(i==structure)flags|=P4_IDLE_VOICE_FRAME_STRUCTURE;
        if(!missing || i!=5)assert(p4_idle_voice_frame_weighted(&window,1384-i*32,flags,i<rough?rough_weight:smooth_weight));
    }
    assert(!p4_idle_voice_frame(&window,968,0));
    return p4_idle_voice_finish(&window);
}

static void idle_noise_evidence_regression(void)
{
    /* Candidate233 pure echo's DAC-quiet empty interval, exactly on the
     * 80% policy boundary. Lower evidence, any voiced structure, or one
     * missing board frame must preserve ordinary ASR behavior. */
    assert(quiet_window(10,8,99,false,1,1)==P4_IDLE_VOICE_ROUGH_UNSTRUCTURED);
    assert(quiet_window(10,7,99,false,1,1)==P4_IDLE_VOICE_INCONCLUSIVE);
    assert(quiet_window(10,8,5,false,1,1)==P4_IDLE_VOICE_INCONCLUSIVE);
    assert(quiet_window(5,5,99,false,1,1)==P4_IDLE_VOICE_INCONCLUSIVE);
    assert(quiet_window(10,8,99,true,1,1)==P4_IDLE_VOICE_UNKNOWN);
    assert(quiet_window(10,6,99,false,32,12)==P4_IDLE_VOICE_ROUGH_UNSTRUCTURED);
    assert(quiet_window(10,6,99,false,32,13)==P4_IDLE_VOICE_INCONCLUSIVE);
    assert(quiet_window(10,6,4,false,32,12)==P4_IDLE_VOICE_INCONCLUSIVE);
    assert(quiet_window(13,6,99,false,192,1)==P4_IDLE_VOICE_INCONCLUSIVE);
    assert(quiet_window(10,5,99,false,192,1)==P4_IDLE_VOICE_INCONCLUSIVE);
    assert(p4_idle_voice_energy_weight(1001,1000)==0);
    assert(p4_idle_voice_energy_weight(4000,2000)==32);
    assert(p4_idle_voice_energy_weight(7000,1000)==192);
    assert(p4_idle_voice_energy_weight(UINT_MAX,1)==192);
    /* DAC overlap/stale history must not supply negative evidence. */
    p4_idle_voice_window_t partial={.from_ms=0,.through_ms=224};
    for(unsigned i=0;i<8;++i)
        assert(p4_idle_voice_frame_weighted(&partial,224-i*32,i==4?0u:13u,1));
    assert(p4_idle_voice_finish(&partial)==P4_IDLE_VOICE_UNKNOWN);
    p4_idle_voice_window_t stale={.from_ms=1000,.through_ms=1384};
    assert(p4_idle_voice_frame_weighted(&stale,1300,13u,192u));
    assert(p4_idle_voice_finish(&stale)==P4_IDLE_VOICE_UNKNOWN);
    int16_t pcm[512];
    for(unsigned i=0;i<512;++i)pcm[i]=500;
    assert(!p4_idle_voice_pcm_rough(pcm));
    for(unsigned i=0;i<512;++i)pcm[i]=(i&1u)?INT16_MIN:INT16_MAX;
    assert(p4_idle_voice_pcm_rough(pcm));
    for(unsigned i=0;i<512;++i)pcm[i]=(int16_t)(1000+(i%80u<40u?100:-100));
    assert(!p4_idle_voice_pcm_rough(pcm));
}

int main(void)
{
    readiness_regression();
    idle_noise_evidence_regression();
    p4_asr_retry_t s={0};
    /* Candidate 145: 500 ms pre-roll + 220 ms voiced candidate + 1,200 ms
     * silence returned empty, then hallucinated P on replay. */
    assert(!p4_asr_retry_has_voice(2424320,2432320,2435840,2455040));
    assert(!s.attempts && !s.pending);
    /* The original full spoken question still qualifies without counting
     * its half-second pre-roll as speech. */
    assert(p4_asr_retry_has_voice(579840,587840,600000,609600));
    assert(!p4_asr_retry_has_voice(10000,18000,24000,43200));
    assert(p4_asr_retry_has_voice(10000,18000,24400,43600));
    assert(!p4_asr_retry_has_voice(10000,9000,24400,43600));
    assert(!p4_asr_retry_has_voice(10000,18000,17000,43600));
    assert(p4_asr_retry_has_voice(UINT_MAX-9999u,UINT_MAX-1999u,4400,23600));
    /* candidate133 cancelled-8: complete 1.86 s submitted, empty final. */
    assert(p4_asr_retry_begin(&s,579840,600000,609600));
    assert(s.start==579840 && s.voice==600000 && s.end==609600);
    s.pending=false;
    assert(!p4_asr_retry_begin(&s,579840,600000,609600));
    /* candidate135: an empty pre-question candidate consumed the global
     * one-shot budget. Distinct later capture must retain its own retry. */
    assert(p4_asr_retry_begin(&s,700000,710000,719600));
    assert(!p4_asr_retry_begin(&s,700000,710000,719600));
    assert(p4_asr_retry_begin(&s,800000,810000,819600));
    assert(!p4_asr_retry_begin(&s,900000,910000,919600));
    p4_asr_retry_accepted(&s);
    assert(p4_asr_retry_begin(&s,700000,710000,719600));
    p4_asr_retry_accepted(&s);
    assert(!p4_asr_retry_begin(&s,10000,11000,13000));
    assert(!p4_asr_retry_begin(&s,10000,25000,20000));
    assert(!p4_asr_retry_begin(&s,10000,20000,138001));
    assert(!p4_asr_retry_begin(&s,10000,9000,20000));
    assert(!s.attempts && !s.pending);
    /* Sample cursors wrap; all policy distances remain bounded. */
    assert(p4_asr_retry_begin(&s,UINT_MAX-9999u,2000,11600));
    return 0;
}
