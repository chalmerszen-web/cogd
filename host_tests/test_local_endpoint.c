#include "source_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t records[SOURCE_METADATA_BYTES];
static agent_endpoint_t endpoint;
static void reset(void)
{ memset(records,0,sizeof(records));assert(!agent_endpoint_init(&endpoint,800,4000,10000)); }
static void frame(unsigned level,unsigned clean,bool spectral,unsigned noise)
{
    unsigned index=endpoint.elapsed_ms/20;
    uint8_t data[6]={(uint8_t)level,(uint8_t)(level>>8),(uint8_t)clean,(uint8_t)(clean>>8),spectral,0xa6};
    memcpy(records+index*6,data,6);
    source_frame_t out;
    assert(!source_stream_endpoint(&endpoint,records,(index+1)*320,noise,&out));
    assert(out.level==level && out.clean_level==clean && out.spectral==spectral);
}
static void resume_boundary(void)
{
    /* Retained failed capture had rising clean energy at its 700-ms boundary.
     * Exercise each position of a resumed onset, not only the ideal600-ms pause.
     * Four valid frames still are necessary to reset the silence clock. */
    for(unsigned pause=600;pause<=680;pause+=20) {
        reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
        for(unsigned i=0;i<20;++i)frame(800,600,true,123);
        for(unsigned ms=0;ms<pause;ms+=20)frame(100,100,false,123);
        for(unsigned i=0;i<4;++i)frame(520,500,true,123);
        assert(endpoint.state==AGENT_EP_SPEECH && !endpoint.quiet_ms);
        assert(endpoint.resume_frames==(pause>=640?(pause-620)/20:0));
        for(unsigned i=0;i<35;++i)frame(100,100,false,123);
        assert(endpoint.state==AGENT_EP_DONE && endpoint.quiet_ms==700);
    }
    /* Every sparse burst of one..three frames near the endpoint remains
     * bounded. It cannot continuously renew the grace or admit another turn. */
    for(unsigned begin=30;begin<35;++begin)for(unsigned length=1;length<=3;++length) {
        reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
        for(unsigned i=0;i<20;++i)frame(800,600,true,100);
        for(unsigned i=0;endpoint.state<AGENT_EP_DONE && i<39;++i)
            frame(800,600,i>=begin && i<begin+length,100);
        assert(endpoint.state==AGENT_EP_DONE && endpoint.quiet_ms>=700 && endpoint.quiet_ms<=760);
        assert(endpoint.resume_frames<=3);
    }
    /* A low-energy or tonal burst is never grounds for grace. */
    for(unsigned kind=0;kind<2;++kind) {
        reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
        for(unsigned i=0;i<20;++i)frame(800,600,true,159);
        for(unsigned i=0;i<35;++i)frame(kind?800:270,kind?40:260,true,159);
        assert(endpoint.state==AGENT_EP_DONE && endpoint.quiet_ms==700 && !endpoint.resume_frames);
    }
    /* Cancellation during grace and the hard10-s limit take precedence. */
    reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
    for(unsigned i=0;i<20;++i)frame(800,600,true,100);
    for(unsigned i=0;i<34;++i)frame(100,100,false,100);
    frame(800,600,true,100);assert(endpoint.resume_frames==1);
    agent_endpoint_cancel(&endpoint);
    agent_endpoint_t saved=endpoint;source_frame_t out;
    assert(source_stream_endpoint(&endpoint,records,40000,100,&out)==AGENT_ERR_BUSY);
    assert(!memcmp(&saved,&endpoint,sizeof(saved)));
    reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
    for(unsigned i=0;i<464;++i)frame(800,600,true,100);
    for(unsigned i=0;i<34;++i)frame(100,100,false,100);
    frame(800,600,true,100);frame(800,600,true,100);
    assert(endpoint.state==AGENT_EP_LIMIT && endpoint.elapsed_ms==10000);
    assert(agent_endpoint_feed_resume(NULL,true)==AGENT_EP_CANCELLED);
}
static void supported_syllable(void)
{
    /* Measured fragmented trailing syllable: three strong frames alone were
     * not enough, despite seven above the lower speech-support threshold.
     * Keep seven-of-eight STRICT onset; only an admitted turn uses support. */
    const unsigned levels[]={328,262,307,383,408,365,343,369};
    reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
    for(unsigned i=0;i<20;++i)frame(800,600,true,184);
    for(unsigned i=0;i<5;++i)frame(100,100,false,184);
    for(unsigned i=0;i<8;++i)frame(levels[i],levels[i],true,184);
    assert(endpoint.state==AGENT_EP_SPEECH && !endpoint.quiet_ms);
    for(unsigned i=0;i<30;++i)frame(100,100,false,184);
    for(unsigned i=0;i<10;++i)frame(800,600,true,184);
    assert(endpoint.state==AGENT_EP_SPEECH && !endpoint.quiet_ms);
    for(unsigned i=0;i<35;++i)frame(100,100,false,184);
    assert(endpoint.state==AGENT_EP_DONE && endpoint.quiet_ms==700);

    /* Weak-only continuation never resets quiet, even immediately after
     * strong speech. One sparse strong pulse also cannot keep it alive. */
    for(unsigned impulses=0;impulses<2;++impulses) {
        reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
        for(unsigned i=0;i<20;++i)frame(800,600,true,159);
        for(unsigned i=0;i<8;++i)frame(100,100,false,159);
        for(unsigned i=0;i<27;++i)frame(impulses && i%16==8?400:270,impulses && i%16==8?400:270,true,159);
        /* The existing60-ms boundary grace can observe the last single pulse;
         * unlike genuine continuation, it must not reset the quiet counter. */
        for(unsigned i=0;i<3 && endpoint.state<AGENT_EP_DONE;++i)frame(270,270,true,159);
        assert(endpoint.state==AGENT_EP_DONE && endpoint.quiet_ms>=700 && endpoint.quiet_ms<=760);
        if(!impulses)assert(endpoint.quiet_ms==700);
    }
    reset();
    for(unsigned i=0;i<200;++i)frame(levels[i%8],levels[i%8],true,184);
    assert(endpoint.state==AGENT_EP_NO_SPEECH); /* Support cannot establish onset. */
    reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
    for(unsigned i=0;i<20;++i)frame(800,600,true,184);
    for(unsigned i=0;i<20;++i)frame(100,100,false,184);
    for(unsigned i=0;i<15;++i)frame(levels[i%8],levels[i%8],true,184);
    for(unsigned i=0;i<3 && endpoint.state<AGENT_EP_DONE;++i)frame(100,100,false,184);
    assert(endpoint.state==AGENT_EP_DONE && endpoint.quiet_ms>=700 && endpoint.quiet_ms<=760);
    assert(agent_endpoint_feed_supported(NULL,true,true)==AGENT_EP_CANCELLED);
}
static void three_vote_boundary(void)
{
    /* Real counterexample: three strong frames, then a60ms gap, then the
     * fourth vote still inside the original160ms confirmation window. */
    for(unsigned outcome=0;outcome<3;++outcome) {
        reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
        for(unsigned i=0;i<20;++i)frame(800,600,true,150);
        for(unsigned i=0;i<31;++i)frame(80,70,false,150);
        for(unsigned i=0;i<3;++i)frame(600,550,true,150);
        assert(endpoint.quiet_ms==680);
        for(unsigned i=0;i<3;++i)frame(280,280,true,150);
        assert(endpoint.state==AGENT_EP_SPEECH && endpoint.quiet_ms==740);
        assert(endpoint.resume_frames==3 && !endpoint.held_frames);
        if(outcome==2) {
            agent_endpoint_cancel(&endpoint);
            assert(agent_endpoint_feed_supported(&endpoint,true,true)==AGENT_EP_CANCELLED);
        } else {
            frame(outcome?600:280,outcome?550:280,true,150);
            assert(endpoint.state==(outcome?AGENT_EP_SPEECH:AGENT_EP_DONE));
            assert(endpoint.quiet_ms==(outcome?0u:760u));
            assert(!endpoint.held_frames);
        }
    }
}
static void transcribed_onset(void)
{
    /* Sparse real syllables can miss seven-of-eight local admission. ASR
     * corroboration admits the current utterance without rewinding its clock. */
    for(unsigned support=0;support<2;++support) {
        reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
        for(unsigned i=0;i<160;++i)frame(600,550,i>=152 && i<158,150);
        assert(endpoint.state==AGENT_EP_WAIT && endpoint.speech_ms==120);
        unsigned quiet=endpoint.quiet_ms;
        if(support) {
            assert(agent_endpoint_transcribed(&endpoint));
            assert(endpoint.transcribed_ms==3200 && endpoint.quiet_ms==quiet);
            agent_endpoint_t saved=endpoint;
            assert(!agent_endpoint_transcribed(&endpoint));
            assert(!memcmp(&endpoint,&saved,sizeof(saved))); /* Repeated text is inert. */
        }
        while(endpoint.state<AGENT_EP_DONE)frame(90,80,false,150);
        assert(endpoint.state==(support?AGENT_EP_DONE:AGENT_EP_NO_SPEECH));
        /* The two silent frames preceding late ASR admission remain measured;
         * observing the admission spends40ms from the same pending budget. */
        assert(endpoint.elapsed_ms==(support?3900u:4000u));
        if(support)assert(endpoint.held_frames==2);
        agent_endpoint_t saved=endpoint;
        assert(!agent_endpoint_transcribed(&endpoint));
        assert(!memcmp(&endpoint,&saved,sizeof(saved))); /* Late text cannot reopen. */
    }
    reset();for(unsigned i=0;i<100;++i)frame(80,70,false,150);
    assert(!agent_endpoint_transcribed(&endpoint)); /* Text alone is insufficient. */
    for(unsigned i=0;i<5;++i)frame(600,550,true,150);
    assert(!agent_endpoint_transcribed(&endpoint));
    agent_endpoint_cancel(&endpoint);
    assert(!agent_endpoint_transcribed(&endpoint));
    assert(!agent_endpoint_transcribed(NULL));
}
static void pending_short_syllable(void)
{
    /* Same shape as the retained failure: fragmented acoustic onset, ASR
     * corroboration, then only two strong frames at the end of a syllable.
     * Sweep the existing support boundary, not the silence threshold. */
    for(unsigned admitted=0;admitted<2;++admitted)
    for(unsigned pending=0;pending<2;++pending)
    for(unsigned gap=260;gap<=340;gap+=20) {
        reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
        for(unsigned i=0;i<6;++i)frame(600,550,true,150);
        if(admitted)assert(agent_endpoint_transcribed(&endpoint));
        agent_endpoint_hint(&endpoint,pending,0);
        unsigned dense=endpoint.dense_at_ms;
        for(unsigned ms=0;ms<gap-40;ms+=20)frame(80,70,false,150);
        frame(600,550,true,150);frame(600,550,true,150);
        bool supported=admitted && pending && gap<=320;
        assert(endpoint.quiet_ms==(supported?0u:gap));
        assert(endpoint.dense_at_ms==dense && !endpoint.held_frames);
        agent_endpoint_hint(&endpoint,false,0);
        while(endpoint.state<AGENT_EP_DONE)frame(80,70,false,150);
        assert(endpoint.state==(admitted?AGENT_EP_DONE:AGENT_EP_NO_SPEECH));
        assert(endpoint.elapsed_ms==(admitted?120u+700u+(supported?gap:0u):4000u));
    }
    /* Weak-only noise and recurring pairs cannot renew the support window.
     * The unchanged pending quota is shared and ends even with stale text. */
    for(unsigned pair=0;pair<2;++pair) {
        reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
        for(unsigned i=0;i<6;++i)frame(600,550,true,150);
        assert(agent_endpoint_transcribed(&endpoint));
        agent_endpoint_hint(&endpoint,true,0);
        for(unsigned i=0;i<8;++i)frame(80,70,false,150);
        for(unsigned i=0;endpoint.state<AGENT_EP_DONE;++i) {
            bool strong=pair && i%8<2;
            frame(strong?600:260,strong?550:250,true,150);
            assert(endpoint.dense_at_ms==120 && endpoint.held_frames<=50);
        }
        assert(endpoint.state==AGENT_EP_DONE && endpoint.elapsed_ms<=2200);
        assert(endpoint.held_frames==50);
        if(!pair)assert(endpoint.elapsed_ms==1820);
    }
    /* Withdrawal revokes the ASR-only admission; two later strong frames
     * cannot reopen it without new corroboration. */
    reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
    for(unsigned i=0;i<6;++i)frame(600,550,true,150);
    agent_endpoint_observe(&endpoint,AGENT_EP_TEXT|AGENT_EP_PENDING);
    assert(endpoint.transcribed_ms==120);
    agent_endpoint_observe(&endpoint,AGENT_EP_GENERATION);
    for(unsigned i=0;i<8;++i)frame(80,70,false,150);
    frame(600,550,true,150);frame(600,550,true,150);
    assert(endpoint.state==AGENT_EP_WAIT && !endpoint.transcribed_ms && endpoint.quiet_ms==200);
    agent_endpoint_cancel(&endpoint);agent_endpoint_t saved=endpoint;
    agent_endpoint_observe(&endpoint,AGENT_EP_TEXT|AGENT_EP_PENDING);
    assert(agent_endpoint_feed_supported(&endpoint,true,true)==AGENT_EP_CANCELLED);
    assert(!memcmp(&saved,&endpoint,sizeof(saved)));
    /* The classic feed deliberately retains its old four-vote rule. */
    assert(!agent_endpoint_init(&endpoint,700,4000,10000));
    for(unsigned i=0;i<6;++i)agent_endpoint_feed(&endpoint,true);
    assert(agent_endpoint_transcribed(&endpoint));agent_endpoint_hint(&endpoint,true,0);
    for(unsigned i=0;i<8;++i)agent_endpoint_feed(&endpoint,false);
    agent_endpoint_feed(&endpoint,true);agent_endpoint_feed(&endpoint,true);
    assert(endpoint.quiet_ms==200 && endpoint.dense_at_ms==120);
}
static void transcribed_short_syllable(void)
{
    /* The saved151 failure has strict local onset, then meaningful text and
     * only two or three strong frames in a trailing syllable. Text need not
     * contain an unfinished argument, but cannot renew the acoustic window. */
    for(unsigned text=0;text<3;++text)for(unsigned gap=260;gap<=340;gap+=20) {
        reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
        for(unsigned i=0;i<8;++i)frame(600,550,true,150);
        assert(endpoint.local_onset && !endpoint.transcribed_ms);
        unsigned notice=text?AGENT_EP_TEXT:0;
        agent_endpoint_observe(&endpoint,notice);
        if(text==2)agent_endpoint_observe(&endpoint,AGENT_EP_GENERATION);
        for(unsigned ms=0;ms<gap-40;ms+=20)frame(80,70,false,150);
        frame(600,550,true,150);frame(600,550,true,150);
        bool supported=text==1 && gap<=320;
        assert(endpoint.quiet_ms==(supported?0u:gap));
        assert(endpoint.dense_at_ms==160 && !endpoint.pending && !endpoint.held_frames);
        while(endpoint.state<AGENT_EP_DONE)frame(80,70,false,150);
        assert(endpoint.state==AGENT_EP_DONE);
        assert(endpoint.elapsed_ms==160+700+(supported?gap:0));
        agent_endpoint_t saved=endpoint;
        agent_endpoint_observe(&endpoint,AGENT_EP_TEXT);
        assert(!memcmp(&saved,&endpoint,sizeof(saved)));
    }
    /* Repeated text, weak-only noise and sparse pairs cannot keep silence
     * open indefinitely. Neither text nor a pair refreshes dense_at_ms. */
    for(unsigned pulses=0;pulses<=2;++pulses) {
        reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
        for(unsigned i=0;i<8;++i)frame(600,550,true,150);
        for(unsigned i=0;i<8;++i)frame(80,70,false,150);
        for(unsigned i=0;endpoint.state<AGENT_EP_DONE;++i) {
            agent_endpoint_observe(&endpoint,AGENT_EP_TEXT);
            bool strong=i%8<pulses;
            frame(strong?600:260,strong?550:250,true,150);
            assert(endpoint.dense_at_ms==160 && !endpoint.held_frames);
        }
        assert(endpoint.state==AGENT_EP_DONE && endpoint.elapsed_ms<=1240);
        if(!pulses)assert(endpoint.elapsed_ms==860);
        if(pulses==1)assert(endpoint.elapsed_ms>=860 && endpoint.elapsed_ms<=920 &&
                            endpoint.quiet_ms>=700); /* Existing bounded boundary grace. */
    }
    /* A meaningful draft preserves the strict classic continuation rule. */
    assert(!agent_endpoint_init(&endpoint,700,4000,10000));
    for(unsigned i=0;i<8;++i)agent_endpoint_feed(&endpoint,true);
    agent_endpoint_observe(&endpoint,AGENT_EP_TEXT);
    for(unsigned i=0;i<8;++i)agent_endpoint_feed(&endpoint,false);
    agent_endpoint_feed(&endpoint,true);agent_endpoint_feed(&endpoint,true);
    assert(endpoint.quiet_ms==200 && endpoint.dense_at_ms==160);
}
int main(void)
{
    transcribed_short_syllable();
    pending_short_syllable();
    transcribed_onset();
    supported_syllable();
    resume_boundary();
    three_vote_boundary();
    /* New fast700-ms hangover keeps the retained600-ms phrase pause. */
    reset();assert(!agent_endpoint_init(&endpoint,700,4000,10000));
    for(unsigned i=0;i<20;++i)frame(800,600,true,100);
    for(unsigned i=0;i<30;++i)frame(100,100,false,100);
    assert(endpoint.state==AGENT_EP_SPEECH);
    for(unsigned i=0;i<20;++i)frame(700,500,true,100);
    for(unsigned i=0;i<34;++i)frame(100,100,false,100);
    assert(endpoint.state==AGENT_EP_SPEECH && endpoint.quiet_ms==680);
    frame(100,100,false,100);
    assert(endpoint.state==AGENT_EP_DONE && endpoint.elapsed_ms==2100);
    reset();
    for(unsigned i=0;i<20;++i)frame(800,600,true,100);
    for(unsigned i=0;i<30;++i)frame(100,100,false,100); /* 600-ms phrase pause. */
    assert(endpoint.state==AGENT_EP_SPEECH);
    for(unsigned i=0;i<20;++i)frame(700,500,true,100);
    for(unsigned i=0;i<39;++i)frame(100,100,false,100);
    assert(endpoint.state==AGENT_EP_SPEECH && endpoint.quiet_ms==780);
    frame(100,100,false,100);
    assert(endpoint.state==AGENT_EP_DONE && endpoint.elapsed_ms==2200);
    agent_endpoint_t saved=endpoint;source_frame_t out;
    assert(source_stream_endpoint(&endpoint,records,40000,100,&out)==AGENT_ERR_BUSY);
    assert(!memcmp(&endpoint,&saved,sizeof(saved)));

    /* Observed filtered microphone tail: level270, clean260, noise159.
     * Even a vendor spectral false positive must not prolong this turn.
     * Retain a600-ms low-level phrase pause before genuine speech resumes. */
    reset();
    for(unsigned i=0;i<20;++i)frame(500,400,true,159);
    for(unsigned i=0;i<30;++i)frame(270,260,true,159);
    assert(endpoint.state==AGENT_EP_SPEECH);
    for(unsigned i=0;i<20;++i)frame(500,400,true,159);
    for(unsigned i=0;i<40;++i)frame(270,260,true,159);
    assert(endpoint.state==AGENT_EP_DONE && endpoint.elapsed_ms==2200);

    /* Tonal noise, spectral rejection and isolated impulses do not admit a turn. */
    for(unsigned mode=0;mode<3;++mode) {
        reset();
        for(unsigned i=0;i<200;++i)frame(800,mode==0?40:600,mode==1?false:mode==2?i%8<3:true,100);
        assert(endpoint.state==AGENT_EP_NO_SPEECH && endpoint.elapsed_ms==4000);
    }
    reset();
    for(unsigned i=0;i<500;++i)frame(800,600,true,100);
    assert(endpoint.state==AGENT_EP_LIMIT); /* Continuous input cannot silently commit. */
    reset();saved=endpoint;
    assert(source_stream_endpoint(&endpoint,records,319,100,&out)==AGENT_ERR_BUSY);
    assert(!memcmp(&endpoint,&saved,sizeof(saved)));
    assert(source_stream_endpoint(&endpoint,records,320,100,&out)==AGENT_ERR_CORRUPT);
    assert(!memcmp(&endpoint,&saved,sizeof(saved)));
    assert(source_stream_endpoint(&endpoint,records,320,32769,&out)==AGENT_ERR_ARGUMENT);
    agent_endpoint_cancel(&endpoint);saved=endpoint;
    assert(source_stream_endpoint(&endpoint,records,320,100,&out)==AGENT_ERR_BUSY);
    assert(!memcmp(&endpoint,&saved,sizeof(saved)));
    puts("local endpoint: sample clock, 600-ms pause, noise guards, publication, limits and cancellation OK");
}
