#include "asr_end.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void until(agent_asr_end_t *s,unsigned end,bool speech)
{
    while(s->source_ms<end) {
        assert(s->source_valid);
        agent_asr_end_source(s,s->source_ms+20,speech);
    }
}
static void phrase(agent_asr_end_t *s)
{ agent_asr_end_reset(s);until(s,300,false);until(s,1000,true); }
static void normal_and_noise(void)
{
    agent_asr_end_t s;
    phrase(&s);agent_asr_end_sentence(&s,1,1000,true,true,true,100);
    assert(!agent_asr_end_ready(&s,true,259));
    assert(!agent_asr_end_ready(&s,false,260));
    assert(agent_asr_end_ready(&s,true,260));
    /* Continuous analogue noise has no new quiet-to-onset transition. The
     * remote final may finish after160ms without another800ms local silence. */
    until(&s,2000,true);
    assert(s.resume_ms==300 && s.quiet_ms==0);
    assert(agent_asr_end_ready(&s,true,260));
    /* A sparse click after a silence is not four votes in160ms. */
    until(&s,2320,false);assert(s.quiet_ms==AGENT_ASR_END_QUIET_MS);
    agent_asr_end_source(&s,2340,true);assert(!s.quiet_ms);
    until(&s,2520,false);agent_asr_end_source(&s,2540,true);
    assert(agent_asr_end_ready(&s,true,300));
}
static void resumed_speech(void)
{
    agent_asr_end_t s,saved;
    phrase(&s);until(&s,1840,false);until(&s,1920,true);
    assert(s.resume_ms==1840);
    /* Reproduce old behavior: the old final arrives after the next phrase
     * starts. The previous bool+confirmed guard would stop here. */
    bool old_final_latched=true,locally_confirmed=true;
    assert(old_final_latched && locally_confirmed);
    agent_asr_end_sentence(&s,1,1000,true,true,true,5000);
    assert(!s.candidate_id && !agent_asr_end_ready(&s,true,5160));
    agent_asr_end_sentence(&s,1,1920,true,true,true,5200);
    assert(!agent_asr_end_ready(&s,true,5360)); /* Duplicate cannot revive. */
    agent_asr_end_sentence(&s,2,1920,true,true,true,5400);
    assert(agent_asr_end_ready(&s,true,5560));

    phrase(&s);until(&s,1840,false);
    agent_asr_end_sentence(&s,1,1000,true,true,true,5000);
    until(&s,1900,true);assert(s.candidate_id); /* Only three votes. */
    agent_asr_end_source(&s,1920,true);assert(!s.candidate_id);
    assert(!agent_asr_end_ready(&s,true,5160));

    /* Retain an original final whose endpoint includes the actual onset;
     * do not confuse the fourth vote's time with a new onset after its end. */
    agent_asr_end_reset(&s);until(&s,400,false);until(&s,460,true);
    agent_asr_end_sentence(&s,1,450,true,true,true,10);
    agent_asr_end_source(&s,480,true);
    assert(s.resume_ms==400 && agent_asr_end_ready(&s,true,170));
    saved=s;agent_asr_end_sentence(&s,1,480,true,true,true,200);
    assert(!memcmp(&s,&saved,sizeof(s)));
}
static void sentence_order(void)
{
    agent_asr_end_t s;
    phrase(&s);agent_asr_end_sentence(&s,1,1000,true,true,true,100);
    agent_asr_end_sentence(&s,2,0,false,false,false,110);
    assert(!agent_asr_end_ready(&s,true,500)); /* Even an empty new partial. */
    agent_asr_end_sentence(&s,1,1000,true,true,true,200);
    assert(!s.candidate_id); /* New partial may precede a delayed old final. */
    agent_asr_end_sentence(&s,2,1000,true,true,true,250);
    assert(!agent_asr_end_ready(&s,true,409));
    assert(agent_asr_end_ready(&s,true,410));
    agent_asr_end_sentence(&s,2,1000,true,true,true,409);
    assert(agent_asr_end_ready(&s,true,410)); /* Duplicate never postpones. */
    agent_asr_end_sentence(&s,3,1000,true,false,true,420);
    assert(!s.candidate_id);
    agent_asr_end_sentence(&s,3,1000,true,true,true,430);
    assert(!s.candidate_id); /* Empty final also closes that sentence ID. */
    agent_asr_end_sentence(&s,2,1000,true,true,true,500);
    assert(!s.candidate_id);
    agent_asr_end_sentence(&s,4,1000,true,true,true,600);
    agent_asr_end_sentence(&s,4,1000,true,false,true,610);
    assert(!agent_asr_end_ready(&s,true,800)); /* Empty duplicate is conservative. */
}
static void invalid_time_and_lag(void)
{
    agent_asr_end_t s;
    const unsigned bad[]={0,10001,UINT32_MAX};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        phrase(&s);agent_asr_end_sentence(&s,1,bad[i],true,true,true,0);
        assert(!agent_asr_end_ready(&s,true,500));
        agent_asr_end_sentence(&s,1,1000,true,true,true,0);
        assert(!agent_asr_end_ready(&s,true,500));
    }
    phrase(&s);agent_asr_end_sentence(&s,1,1000,true,true,false,0);
    assert(!agent_asr_end_ready(&s,true,500));
    agent_asr_end_sentence(&s,0,1000,true,true,true,0);
    assert(!s.candidate_id);
    agent_asr_end_reset(&s);until(&s,500,true);
    agent_asr_end_sentence(&s,1,1000,true,true,true,100);
    assert(!agent_asr_end_ready(&s,true,260));
    until(&s,980,true);assert(!agent_asr_end_ready(&s,true,300));
    until(&s,1000,true);assert(agent_asr_end_ready(&s,true,300));
    agent_asr_end_source(&s,1040,false); /* Missing1020 invalidates assistance. */
    assert(!s.source_valid && !agent_asr_end_ready(&s,true,500));
    agent_asr_end_sentence(&s,2,1040,true,true,true,600);
    agent_asr_end_source(&s,1020,false);
    assert(!agent_asr_end_ready(&s,true,800));
    phrase(&s);agent_asr_end_source(&s,1000,false);assert(!s.source_valid);
}
static void wrap_and_reset(void)
{
    agent_asr_end_t s;
    phrase(&s);agent_asr_end_sentence(&s,1,1000,true,true,true,UINT32_MAX-79u);
    assert(!agent_asr_end_ready(&s,true,79));
    assert(agent_asr_end_ready(&s,true,80));
    agent_asr_end_reset(&s);
    assert(s.source_valid && !s.source_ms && !s.newest_id && !s.candidate_id);
    assert(!agent_asr_end_ready(&s,true,1000));
    until(&s,10000,true);agent_asr_end_sentence(&s,1,10000,true,true,true,0);
    assert(agent_asr_end_ready(&s,true,160));
    agent_asr_end_source(&s,10020,true);assert(!s.source_valid);
    agent_asr_end_reset(NULL);agent_asr_end_source(NULL,20,true);
    agent_asr_end_sentence(NULL,1,20,true,true,true,0);
    assert(!agent_asr_end_ready(NULL,true,160));
}
int main(void)
{
    agent_asr_end_t s;phrase(&s);
    agent_asr_end_sentence(&s,1,1000,true,true,true,100);
    until(&s,1680,false);assert(!agent_asr_end_ready_after(&s,true,999,700));
    until(&s,1700,false);assert(agent_asr_end_ready_after(&s,true,999,700));
    until(&s,1780,true);assert(!agent_asr_end_ready_after(&s,true,999,700));
    agent_asr_end_sentence(&s,2,1780,true,true,true,1000);
    until(&s,2480,false);assert(agent_asr_end_ready_after(&s,true,1160,700));
    assert(!agent_asr_end_ready_after(&s,false,1160,700));
    assert(!agent_asr_end_ready_after(&s,true,1160,UINT32_MAX));
    /* A late cloud final must not end uninterrupted local speech: there was
     * no300ms gap to arm the previous resume-only detector. */
    phrase(&s);until(&s,1800,true);
    agent_asr_end_sentence(&s,1,1000,true,true,true,100);
    assert(agent_asr_end_ready(&s,true,1000));
    assert(!agent_asr_end_ready_after(&s,true,1000,700));
    until(&s,2500,false);
    assert(!agent_asr_end_ready_after(&s,true,1000,700));
    agent_asr_end_sentence(&s,2,1800,true,true,true,1000);
    assert(agent_asr_end_ready_after(&s,true,1160,700));
    normal_and_noise();resumed_speech();sentence_order();invalid_time_and_lag();wrap_and_reset();
    printf("asr_end: %zu-byte guard; late finals, resumed speech, source lag, noise, order, wrap and reset passed\n",sizeof(agent_asr_end_t));
}
