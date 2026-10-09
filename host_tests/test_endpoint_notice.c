#include "endpoint.h"
#include "source_bound.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static void start(agent_endpoint_t *e)
{ assert(!agent_endpoint_init(e,700,4000,10000)); }
static void frames(agent_endpoint_t *e,unsigned n,bool speech)
{ while(n--)agent_endpoint_feed_supported(e,speech,speech); }
static unsigned text(unsigned before,bool meaningful,bool pending,bool empty)
{ return agent_endpoint_notice(before,meaningful,pending,empty,true); }
static void asr_only(agent_endpoint_t *e)
{
    start(e);frames(e,6,true);agent_endpoint_observe(e,text(0,true,false,false));
    assert(e->state==AGENT_EP_SPEECH && e->transcribed_ms==120 && !e->local_onset);
}
static void coalesced_and_fresh(void)
{
    agent_endpoint_t e;asr_only(&e);
    atomic_uint mailbox=ATOMIC_VAR_INIT(text(0,true,true,false));
    agent_endpoint_observe(&e,atomic_load(&mailbox));frames(&e,35,false);
    assert(e.held_frames==1 && e.quiet_ms==700);
    /* The owner is not scheduled between the empty receipt and new draft. */
    atomic_store(&mailbox,text(atomic_load(&mailbox),false,false,true));
    atomic_store(&mailbox,text(atomic_load(&mailbox),true,false,false));
    agent_endpoint_observe(&e,atomic_load(&mailbox));
    assert(e.state==AGENT_EP_WAIT && !e.transcribed_ms && e.asr_floor_ms==120);
    assert(e.elapsed_ms==820 && e.quiet_ms==700 && e.held_frames==1);
    for(unsigned i=0;i<6;++i) {
        agent_endpoint_observe(&e,atomic_load(&mailbox));assert(e.state==AGENT_EP_WAIT);
        frames(&e,1,true);
    }
    assert(!e.local_onset && e.speech_ms-e.asr_floor_ms==120);
    agent_endpoint_observe(&e,atomic_load(&mailbox));
    assert(e.state==AGENT_EP_SPEECH && e.transcribed_ms==940 && e.held_frames==1);
    /* A second empty resets only the floor, not the clock or spent quota. */
    atomic_store(&mailbox,text(atomic_load(&mailbox),false,false,true));
    agent_endpoint_observe(&e,atomic_load(&mailbox));
    assert(e.state==AGENT_EP_WAIT && e.asr_floor_ms==240 && e.elapsed_ms==940 && e.held_frames==1);
    atomic_store(&mailbox,text(atomic_load(&mailbox),true,false,false));
    for(unsigned i=0;i<200;++i) {agent_endpoint_observe(&e,atomic_load(&mailbox));frames(&e,1,false);}
    assert(e.state==AGENT_EP_NO_SPEECH && e.elapsed_ms==4000 && !e.transcribed_ms);
}
static void independent_and_short(void)
{
    for(unsigned earlier=0;earlier<2;++earlier) {
        agent_endpoint_t e;
        if(earlier) {start(&e);frames(&e,7,true);}
        else {asr_only(&e);frames(&e,1,true);}
        assert(e.local_onset);
        agent_endpoint_observe(&e,text(e.asr_notice,false,false,true));
        assert(e.state==AGENT_EP_SPEECH && !e.asr_floor_ms);
        frames(&e,35,false);assert(e.state==AGENT_EP_DONE && e.elapsed_ms==840);
    }
    agent_endpoint_t e;asr_only(&e);
    agent_endpoint_observe(&e,text(e.asr_notice,false,false,false)); /* 请 */
    assert(e.state==AGENT_EP_SPEECH && e.transcribed_ms==120 && !e.asr_floor_ms);
    frames(&e,35,false);assert(e.state==AGENT_EP_DONE && e.elapsed_ms==820);
    start(&e);
    agent_endpoint_observe(&e,text(0,false,false,true));frames(&e,6,true);
    agent_endpoint_observe(&e,text(e.asr_notice,true,false,false));
    assert(e.state==AGENT_EP_SPEECH && !e.asr_floor_ms); /* Empty before any ASR admission. */
}
static void lifecycle_and_limits(void)
{
    for(unsigned terminal=0;terminal<4;++terminal) {
        agent_endpoint_t e;asr_only(&e);
        if(terminal==0)frames(&e,35,false);
        else if(terminal==1)agent_endpoint_cancel(&e);
        else if(terminal==2) {
            agent_endpoint_observe(&e,text(e.asr_notice,false,false,true));frames(&e,200,false);
        } else frames(&e,500,true);
        assert(e.state>=AGENT_EP_DONE);agent_endpoint_t before=e;
        unsigned notice=text(e.asr_notice,false,false,true);
        notice=text(notice,true,true,false);notice=agent_endpoint_proposal(notice,20);
        agent_endpoint_observe(&e,notice);frames(&e,100,true);
        assert(!memcmp(&before,&e,sizeof(e)));
        start(&e);assert(!e.asr_floor_ms && !e.asr_notice && !e.transcribed_ms && !e.local_onset);
        /* Reset mailbox only after the preceding producers have joined. */
        agent_endpoint_observe(&e,0);frames(&e,200,false);
        assert(e.state==AGENT_EP_NO_SPEECH && e.elapsed_ms==4000);
    }
    agent_endpoint_t e;start(&e);
    for(unsigned i=0;i<200;++i) {
        if(i==6)agent_endpoint_observe(&e,text(0,true,false,false));
        frames(&e,1,i%20<6); /* Continuing sparse speech, never seven votes. */
    }
    assert(e.state==AGENT_EP_SPEECH && !e.local_onset && e.elapsed_ms==4000);
    agent_endpoint_observe(&e,text(e.asr_notice,false,false,true));
    agent_endpoint_observe(&e,text(e.asr_notice,true,false,false));frames(&e,1,true);
    assert(e.state==AGENT_EP_NO_SPEECH && e.elapsed_ms==4020); /* Waiting limit is not renewed. */
}
static void coherent_cloud(void)
{
    agent_endpoint_t e;asr_only(&e);
    unsigned notice=agent_endpoint_proposal(e.asr_notice,140);
    assert((notice&AGENT_EP_TEXT) && (notice&AGENT_EP_REMOTE_MASK));
    notice=text(notice,false,false,true);notice=text(notice,true,true,false);
    assert((notice&~(AGENT_EP_GENERATION-1u))==AGENT_EP_GENERATION);
    assert(!(notice&AGENT_EP_REMOTE_MASK)); /* No old cloud end with new flags. */
    agent_endpoint_observe(&e,notice);frames(&e,1,false);
    assert(e.state==AGENT_EP_WAIT && !e.remote_ms && !e.held_frames);
    notice=agent_endpoint_proposal(notice,400);
    agent_endpoint_observe(&e,notice);frames(&e,6,true);
    agent_endpoint_observe(&e,notice);
    assert(e.state==AGENT_EP_SPEECH && e.remote_ms==400 && !e.held_frames);
    frames(&e,6,false);assert(e.elapsed_ms==380 && !e.held_frames);
    frames(&e,1,false);assert(e.held_frames==1 && e.elapsed_ms==400);
    notice=agent_endpoint_proposal(notice,401);agent_endpoint_observe(&e,notice);
    assert(!e.remote_ms && e.held_frames==1);
    assert(agent_endpoint_proposal(notice,10001)==agent_endpoint_proposal(notice,0));
    unsigned before=notice;
    for(unsigned i=0;i<1000;++i) {notice=text(notice,false,false,true);notice=text(notice,true,true,false);}
    assert(notice/AGENT_EP_GENERATION==before/AGENT_EP_GENERATION+1000);
    /* Unsigned generation wrap is defined; 2^20 empties in a10s turn are impossible. */
    notice=text(~(AGENT_EP_GENERATION-1u),true,false,true);
    assert(notice==AGENT_EP_TEXT);
}
static void producer_bound_with_withdrawal(void)
{
    for(unsigned seed=1;seed<=128;++seed) {
        source_bound_t b;agent_endpoint_t e;start(&e);
        assert(!source_bound_init(&b,100));b.end_grace_ms=AGENT_EP_PENDING_MS;
        unsigned notice=0,rng=seed;
        for(unsigned frame=0;!b.target_samples;++frame) {
            rng=rng*1664525u+1013904223u;
            bool possible=frame<seed+100 && (rng&255)>25;
            bool weak=possible && ((rng>>8)&255)>40;
            bool strong=weak && ((rng>>16)&255)>70;
            if(frame%11==0)notice=text(notice,false,false,true);
            if(frame%7==0)notice=text(notice,true,true,false);
            if(frame%17==0)notice=agent_endpoint_proposal(notice,frame*20);
            agent_endpoint_observe(&e,notice);
            assert(!source_bound_feed(&b,possible?600:80));
            agent_endpoint_feed_supported(&e,strong,weak);
        }
        assert(e.state>=AGENT_EP_DONE && e.elapsed_ms<=b.elapsed_ms && e.held_frames<=50);
    }
}
static void short_revision_boundary(void)
{
    /* Every Boolean publication combination. A short draft preserves only
     * pending, not old text confirmation or a cloud proposal; empty still
     * increments the withdrawal generation. */
    for(unsigned old=0;old<4;++old)for(unsigned flags=0;flags<8;++flags) {
        bool meaningful=flags&1u,pending=flags&2u,empty=flags&4u;
        unsigned before=agent_endpoint_proposal(7*AGENT_EP_GENERATION+old,2000);
        unsigned notice=text(before,meaningful,pending,empty);
        assert((notice&AGENT_EP_TEXT)==(meaningful?AGENT_EP_TEXT:0));
        assert(!!(notice&AGENT_EP_PENDING)==(pending || (!meaningful && !empty && (old&AGENT_EP_PENDING))));
        assert(!(notice&AGENT_EP_REMOTE_MASK));
        assert(notice/AGENT_EP_GENERATION==7u+empty);
    }
    /* A live owner sees an insufficient revised prefix exactly when silence
     * is already eligible. It gets the remaining quota, never a fresh quota. */
    for(unsigned spent=0;spent<=50;++spent) {
        agent_endpoint_t e;start(&e);frames(&e,20,true);
        unsigned notice=text(0,true,true,false);agent_endpoint_observe(&e,notice);
        frames(&e,34+spent,false);assert(e.held_frames==spent);
        notice=text(notice,false,false,false); /* e.g. rewritten draft "请" */
        agent_endpoint_observe(&e,notice);
        while(e.state<AGENT_EP_DONE) {
            for(unsigned i=0;i<100;++i) {
                notice=text(notice,false,false,false);agent_endpoint_observe(&e,notice);
            }
            frames(&e,1,false);
        }
        assert(e.elapsed_ms==2100 && e.held_frames==50 && !e.resume_frames);
        agent_endpoint_t terminal=e;agent_endpoint_observe(&e,text(notice,true,true,false));
        frames(&e,10,true);assert(!memcmp(&e,&terminal,sizeof(e)));
        for(unsigned clear=0;clear<2;++clear) {
            start(&e);frames(&e,20,true);notice=text(0,true,true,false);
            agent_endpoint_observe(&e,notice);frames(&e,34+spent,false);
            notice=text(notice,!clear,false,clear); /* Complete/new intent OR empty withdrawal. */
            agent_endpoint_observe(&e,notice);frames(&e,1,false);
            assert(e.state==AGENT_EP_DONE && e.elapsed_ms==1100+spent*20 && e.held_frames==spent);
        }
    }
    agent_endpoint_t e;start(&e);
    unsigned notice=0;
    for(unsigned i=0;i<200;++i) {
        notice=text(notice,false,false,false);agent_endpoint_observe(&e,notice);
        frames(&e,1,false);
    }
    assert(e.state==AGENT_EP_NO_SPEECH && !e.held_frames && !e.pending);
    asr_only(&e);notice=text(e.asr_notice,true,true,false);
    agent_endpoint_observe(&e,notice);frames(&e,35,false);
    notice=text(notice,false,false,true);notice=text(notice,false,false,false);
    agent_endpoint_observe(&e,notice);
    assert(e.state==AGENT_EP_WAIT && !e.pending && e.held_frames==1 && e.asr_floor_ms==120);
    agent_endpoint_cancel(&e);agent_endpoint_t cancelled=e;
    agent_endpoint_observe(&e,text(notice,true,true,false));frames(&e,10,true);
    assert(!memcmp(&e,&cancelled,sizeof(e)));
}
int main(void)
{
    coalesced_and_fresh();independent_and_short();lifecycle_and_limits();coherent_cloud();
    producer_bound_with_withdrawal();short_revision_boundary();puts("ASR withdrawal owner invariants passed");
}
