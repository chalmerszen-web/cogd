#include "endpoint.h"
#include "intent.h"
#include "source_bound.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void vocabulary(void)
{
    const char *yes[]={"请把","請將。","唔該，把燈","请把灯调。","请把灯调成。",
        "唔該將燈調成","请把灯调成蓝色，不对。","把灯调成蓝色，不对，不要",
        "把燈改做藍色，唔係，改做","记住。","请记住，","請記住……",
        "唔該記低。","记低","請，記住。"};
    const char *no[]={"","请","你好","你是谁","帮我想一想","把灯调成蓝色。",
        "唔該將燈改做綠色。","请把灯调成蓝色，不对，不要蓝色，改成绿色。",
        "请把灯调成蓝色，不对，取消。","这句话对不对？","他说“请把灯调成”",
        "请把书给我","请把灯关掉","取消","唔使","hello","你说的不对",
        "请记住灯名叫小星星。","記低盞燈叫小星星。","不用记住", "唔使記低",
        "请记住，算了。","他说“记住”","你记住了吗？"};
    assert(!agent_speech_pending_argument(NULL));
    for(unsigned i=0;i<sizeof(yes)/sizeof(*yes);++i)assert(agent_speech_pending_argument(yes[i]));
    for(unsigned i=0;i<sizeof(no)/sizeof(*no);++i)assert(!agent_speech_pending_argument(no[i]));
}
static void start(agent_endpoint_t *e)
{ assert(!agent_endpoint_init(e,700,4000,10000)); }
static void feed(agent_endpoint_t *e,unsigned count,bool speech)
{ while(count--)agent_endpoint_feed_supported(e,speech,speech); }
static void phrase_notice(agent_endpoint_t *e,const char *text)
{
    agent_endpoint_observe(e,agent_endpoint_notice_with_phrase(e->asr_notice,
        agent_speech_meaningful_partial(text),agent_speech_pending_argument(text),
        !text[strspn(text," \t\r\n")],false,agent_speech_phrase_pause(text)));
}
static void sentence_pause(void)
{
    const char *yes[]={"为什么", "为什么天空是蓝色的？", "如何解释", "點解個天係藍色？", "点样做"};
    const char *no[]={"", "你好", "你是谁", "你叫什么名字", "请用一句话介绍你自己。",
        "请把灯调成蓝色。", "唔該將燈改做綠色。", "请记住灯名叫小星星。", "取消"};
    assert(!agent_speech_phrase_pause(NULL));
    for(unsigned i=0;i<sizeof(yes)/sizeof(*yes);++i)assert(agent_speech_phrase_pause(yes[i]));
    for(unsigned i=0;i<sizeof(no)/sizeof(*no);++i)assert(!agent_speech_phrase_pause(no[i]));
    agent_endpoint_t e;start(&e);feed(&e,20,true);phrase_notice(&e,yes[1]);
    feed(&e,46,false);assert(e.state==AGENT_EP_SPEECH && e.quiet_ms==920 && e.held_frames==12);
    feed(&e,20,true);assert(!e.quiet_ms && e.held_frames==15);
    while(e.state<AGENT_EP_DONE) {phrase_notice(&e,yes[1]);feed(&e,1,false);}
    assert(e.elapsed_ms==2520 && e.held_frames==20); /* Two pauses share400ms. */
    agent_endpoint_t terminal=e;phrase_notice(&e,yes[3]);feed(&e,10,true);
    assert(!memcmp(&e,&terminal,sizeof(e)));

    /* Repeated partials, punctuation and new questions never renew the quota. */
    for(unsigned i=0;i<sizeof(yes)/sizeof(*yes);++i) {
        start(&e);feed(&e,20,true);
        while(e.state<AGENT_EP_DONE) {phrase_notice(&e,yes[i]);feed(&e,1,false);}
        assert(e.elapsed_ms==1500 && e.held_frames==20);
    }
    /* A sentence starting at the last held frame can fill the original four
     * strong votes; one to three isolated impulses cannot keep capture alive. */
    for(unsigned run=1;run<=4;++run) {
        start(&e);feed(&e,20,true);phrase_notice(&e,yes[1]);feed(&e,54,false);
        assert(e.held_frames==20 && e.quiet_ms==1080);
        feed(&e,run,true);assert(e.state==AGENT_EP_SPEECH);
        feed(&e,1,false);
        assert(e.state==(run==4?AGENT_EP_SPEECH:AGENT_EP_DONE));
    }
    /* An explicit unfinished argument can use only the remaining original
     * quota; switching back to phrase mode never gives it another400ms. */
    start(&e);feed(&e,20,true);phrase_notice(&e,yes[1]);feed(&e,50,false);
    assert(e.held_frames==16);phrase_notice(&e,"请把灯调成");feed(&e,30,false);
    assert(e.held_frames==46);phrase_notice(&e,yes[1]);feed(&e,1,false);
    assert(e.state==AGENT_EP_DONE && e.held_frames==46);

    /* No new wait for ordinary short commands, a changed intent or classic. */
    for(unsigned i=0;i<sizeof(no)/sizeof(*no);++i) {
        start(&e);feed(&e,20,true);phrase_notice(&e,no[i]);feed(&e,35,false);
        assert(e.state==AGENT_EP_DONE && e.elapsed_ms==1100 && !e.held_frames);
    }
    start(&e);feed(&e,20,true);phrase_notice(&e,yes[1]);feed(&e,40,false);
    phrase_notice(&e,"你是谁");feed(&e,1,false);
    assert(e.state==AGENT_EP_DONE && e.held_frames==6);
    start(&e);feed(&e,20,true);phrase_notice(&e,yes[1]);
    for(unsigned i=0;i<35;++i)agent_endpoint_feed(&e,false);
    assert(e.state==AGENT_EP_DONE && !e.held_frames);
    start(&e);phrase_notice(&e,yes[1]);feed(&e,200,false);
    assert(e.state==AGENT_EP_NO_SPEECH && !e.held_frames);
    start(&e);feed(&e,20,true);phrase_notice(&e,yes[1]);agent_endpoint_cancel(&e);terminal=e;
    phrase_notice(&e,yes[3]);feed(&e,100,true);assert(!memcmp(&e,&terminal,sizeof(e)));
    start(&e);feed(&e,465,true);phrase_notice(&e,yes[1]);feed(&e,35,false);
    assert(e.state==AGENT_EP_LIMIT && e.elapsed_ms==10000);

    /* The mailbox keeps only an insufficient revision's pause hint, removes
     * stale cloud proposals, and coalesces an empty withdrawal with new text. */
    unsigned notice=agent_endpoint_notice_with_phrase(0,true,false,false,false,true);
    notice=agent_endpoint_proposal(notice,2000);
    notice=agent_endpoint_notice_with_phrase(notice,false,false,false,false,false);
    assert((notice&AGENT_EP_PHRASE) && !(notice&(AGENT_EP_TEXT|AGENT_EP_REMOTE_MASK)));
    notice=agent_endpoint_notice_with_phrase(notice,false,false,true,false,true);
    assert(notice==AGENT_EP_GENERATION);
    notice=agent_endpoint_notice_with_phrase(notice,true,false,false,false,true);
    assert(notice==(AGENT_EP_GENERATION|AGENT_EP_TEXT|AGENT_EP_PHRASE));
}

static void memory_pause(void)
{
    const char *prefixes[]={"请记住。","唔該記低。"};
    for(unsigned i=0;i<sizeof(prefixes)/sizeof(*prefixes);++i) {
        agent_endpoint_t e;start(&e);feed(&e,20,true);
        unsigned notice=agent_endpoint_notice(0,true,
            agent_speech_pending_argument(prefixes[i]),false,false);
        agent_endpoint_observe(&e,notice);feed(&e,40,false);
        assert(e.state==AGENT_EP_SPEECH && e.quiet_ms==800 && e.held_frames==6);
        feed(&e,4,true);assert(e.state==AGENT_EP_SPEECH && !e.quiet_ms);
        notice=agent_endpoint_notice(notice,true,
            agent_speech_pending_argument("请记住灯名叫小星星。"),false,true);
        agent_endpoint_observe(&e,notice);feed(&e,35,false);
        assert(e.state==AGENT_EP_DONE && e.elapsed_ms==1980 && e.held_frames<=50);

        /* Repeated bare-verb drafts may not extend the old per-turn quota. */
        start(&e);feed(&e,20,true);
        while(e.state<AGENT_EP_DONE) {
            agent_endpoint_observe(&e,agent_endpoint_notice(0,true,
                agent_speech_pending_argument(prefixes[i]),false,false));
            feed(&e,1,false);
        }
        assert(e.elapsed_ms==2100 && e.held_frames==50);
    }
}

static void cumulative(void)
{
    agent_endpoint_t e;start(&e);agent_endpoint_hint(&e,true,0);
    feed(&e,20,true);feed(&e,40,false);feed(&e,20,true);feed(&e,200,false);
    assert(e.state==AGENT_EP_DONE && e.elapsed_ms==3120 && e.held_frames==50);
    agent_endpoint_t reference=e;
    start(&e);
    for(unsigned i=0;i<280;++i) {
        agent_endpoint_hint(&e,true,0); /* Repeated hints never refill. */
        feed(&e,1,i<20 || (i>=60 && i<80));
    }
    assert(!memcmp(&e,&reference,sizeof(e)));
    agent_endpoint_hint(&e,false,0);feed(&e,100,true);
    assert(!memcmp(&e,&reference,sizeof(e))); /* Terminal cannot reopen. */

    start(&e);feed(&e,20,true);agent_endpoint_hint(&e,true,0);feed(&e,35,false);
    assert(e.held_frames==1 && e.elapsed_ms==1100);
    agent_endpoint_hint(&e,false,0);feed(&e,1,false);
    assert(e.state==AGENT_EP_DONE && e.elapsed_ms==1120);
    start(&e);feed(&e,20,true);agent_endpoint_hint(&e,true,0);feed(&e,35,false);
    agent_endpoint_cancel(&e);reference=e;
    agent_endpoint_hint(&e,true,1000);feed(&e,500,true);
    assert(e.state==AGENT_EP_CANCELLED && !memcmp(&e,&reference,sizeof(e)));
}
static agent_endpoint_t exhausted(void)
{
    agent_endpoint_t e;start(&e);feed(&e,20,true);agent_endpoint_hint(&e,true,0);
    feed(&e,84,false);
    assert(e.state==AGENT_EP_SPEECH && e.held_frames==50 && e.quiet_ms==1680);
    return e;
}
static void late_resume(void)
{
    /* A syllable starts while the last pending frame is spent. It must get
     * its four existing strong votes, without treating sparse noise as speech. */
    agent_endpoint_t e=exhausted();feed(&e,4,true);
    assert(e.state==AGENT_EP_SPEECH && !e.quiet_ms && e.held_frames==50 && e.resume_frames==3);
    feed(&e,35,false);assert(e.state==AGENT_EP_DONE && e.elapsed_ms==2860);
    for(unsigned run=1;run<=3;++run) {
        e=exhausted();feed(&e,run,true);assert(e.state==AGENT_EP_SPEECH);
        feed(&e,1,false);assert(e.state==AGENT_EP_DONE && e.resume_frames==run && e.held_frames==50);
        agent_endpoint_t terminal=e;feed(&e,100,true);assert(!memcmp(&e,&terminal,sizeof(e)));
    }
    e=exhausted();agent_endpoint_feed_supported(&e,false,true);
    assert(e.state==AGENT_EP_DONE && !e.resume_frames);
    e=exhausted();agent_endpoint_hint(&e,false,0);feed(&e,1,true);
    assert(e.state==AGENT_EP_DONE && !e.resume_frames);
    e=exhausted();agent_endpoint_cancel(&e);agent_endpoint_t cancelled=e;feed(&e,4,true);
    assert(!memcmp(&e,&cancelled,sizeof(e)));
    start(&e);feed(&e,465,true);feed(&e,34,false);
    e.held_frames=50;agent_endpoint_hint(&e,true,0);feed(&e,1,true);
    assert(e.state==AGENT_EP_LIMIT && e.elapsed_ms==10000);
}
static void cloud_proposals(void)
{
    agent_endpoint_t e;start(&e);feed(&e,20,true);feed(&e,34,false);
    /* Local and cloud proposals coincide. Only the next source frame spends. */
    for(unsigned j=0;j<50;++j) {
        for(unsigned i=0;i<100;++i)agent_endpoint_hint(&e,true,e.elapsed_ms+20);
        assert(e.held_frames==j);feed(&e,1,false);
        assert(e.state==AGENT_EP_SPEECH && e.held_frames==j+1);
    }
    feed(&e,1,false);assert(e.state==AGENT_EP_DONE && e.elapsed_ms==2100);

    /* The independent cloud guard may be eligible before a longer local end.
     * Revoke that proposal, resume speech, then reuse the SAME remaining quota. */
    assert(!agent_endpoint_init(&e,2000,4000,10000));feed(&e,20,true);
    agent_endpoint_hint(&e,true,1100);feed(&e,34,false);
    assert(!e.held_frames);feed(&e,1,false);assert(e.held_frames==1);
    agent_endpoint_hint(&e,true,0);feed(&e,20,true);
    assert(e.held_frames==1 && !e.quiet_ms);
    agent_endpoint_hint(&e,true,2200);feed(&e,34,false);assert(e.held_frames==1);
    feed(&e,49,false);assert(e.state==AGENT_EP_SPEECH && e.held_frames==50);
    feed(&e,1,false);assert(e.state==AGENT_EP_DONE && e.elapsed_ms==3180);

    start(&e);agent_endpoint_hint(&e,true,20);feed(&e,200,false);
    assert(e.state==AGENT_EP_NO_SPEECH && !e.held_frames);
    start(&e);feed(&e,20,true);
    agent_endpoint_hint(&e,false,10000);feed(&e,1,false);
    assert(e.state==AGENT_EP_SPEECH); /* Future source coverage not consumed. */
    agent_endpoint_hint(&e,false,10001);assert(!e.remote_ms);
    agent_endpoint_hint(&e,false,419);assert(!e.remote_ms);
    agent_endpoint_hint(&e,false,420);feed(&e,1,false);
    assert(e.state==AGENT_EP_DONE && !e.held_frames);
}
static void hard_limits(void)
{
    agent_endpoint_t e;start(&e);agent_endpoint_hint(&e,true,0);feed(&e,465,true);feed(&e,35,false);
    assert(e.state==AGENT_EP_LIMIT && e.elapsed_ms==10000 && !e.held_frames);
    start(&e);agent_endpoint_hint(&e,true,0);feed(&e,460,true);feed(&e,40,false);
    assert(e.state==AGENT_EP_LIMIT && e.elapsed_ms==10000 && e.held_frames==5);
    start(&e);agent_endpoint_hint(&e,true,0);feed(&e,200,false);
    assert(e.state==AGENT_EP_NO_SPEECH && e.elapsed_ms==4000 && !e.held_frames);
}
static void admit_late(agent_endpoint_t *e,unsigned at)
{
    feed(e,6,true);feed(e,(at-120)/20,false);
    assert(e->state==AGENT_EP_WAIT && e->elapsed_ms==at);
    assert(agent_endpoint_transcribed(e));
    assert(e->transcribed_ms==at && !e->local_onset);
}
static void fresh_admission(void)
{
    /* Every possible pre-timeout admission: retain the old quiet clock and
     * cap added observation by the original per-capture quota. */
    const unsigned ends[]={400,700,1000,2000};
    for(unsigned j=0;j<sizeof(ends)/sizeof(*ends);++j)for(unsigned at=120;at<4000;at+=20) {
        agent_endpoint_t e;assert(!agent_endpoint_init(&e,ends[j],4000,10000));
        admit_late(&e,at);unsigned quiet=e.quiet_ms;
        unsigned base=at+20>120+ends[j]?at+20:120+ends[j];
        unsigned expected=at+ends[j];if(expected>base+1000)expected=base+1000;
        while(e.state<AGENT_EP_DONE) {
            unsigned stamp=e.transcribed_ms;
            assert(!agent_endpoint_transcribed(&e));assert(e.transcribed_ms==stamp);
            feed(&e,1,false);
        }
        assert(e.state==AGENT_EP_DONE && e.elapsed_ms==expected && e.held_frames<=50);
        assert(e.quiet_ms==quiet+expected-at && !e.resume_frames);
    }
    agent_endpoint_t e;start(&e);admit_late(&e,3800);feed(&e,1,false);
    assert(e.state==AGENT_EP_SPEECH && e.quiet_ms==3700 && e.held_frames==1);
    agent_endpoint_hint(&e,true,0);feed(&e,49,false);
    assert(e.state==AGENT_EP_SPEECH && e.held_frames==50);
    feed(&e,1,false);assert(e.state==AGENT_EP_DONE && e.elapsed_ms==4820);
    /* A cloud proposal already satisfied its independent temporal/evidence
     * guard. Freshness alone must not add any wait to it. */
    start(&e);admit_late(&e,3800);agent_endpoint_hint(&e,false,3820);feed(&e,1,false);
    assert(e.state==AGENT_EP_DONE && e.elapsed_ms==3820 && !e.held_frames);
    /* Strict local onset similarly needs no new ASR observation window. */
    start(&e);feed(&e,7,true);e.transcribed_ms=140;feed(&e,35,false);
    assert(e.state==AGENT_EP_DONE && e.elapsed_ms==840 && !e.held_frames);
    /* The non-resuming classic feed keeps its original endpoint semantics. */
    start(&e);admit_late(&e,3800);agent_endpoint_feed(&e,false);
    assert(e.state==AGENT_EP_DONE && e.elapsed_ms==3820 && !e.held_frames);
    start(&e);admit_late(&e,3800);agent_endpoint_cancel(&e);feed(&e,1,true);
    assert(e.state==AGENT_EP_CANCELLED && e.elapsed_ms==3800);
    /* A withdrawal preserves spent quota and the original waiting deadline. */
    start(&e);admit_late(&e,3800);feed(&e,9,false);
    agent_endpoint_observe(&e,agent_endpoint_notice(0,false,false,true,true));
    feed(&e,1,true);assert(e.state==AGENT_EP_NO_SPEECH && e.elapsed_ms==4000 && e.held_frames==9);
    assert(!agent_endpoint_transcribed(&e));
}
static void late_producer_bounds(void)
{
    /* Late ASR-only admission needs conservative acquisition beyond4s even
     * if all energy evidence was old. Adversarial new runs can straddle the
     * shared quota boundary; a producer may stop only after the consumer. */
    for(unsigned at=120;at<4000;at+=20)for(unsigned pending=0;pending<2;++pending)
    for(unsigned run=0;run<=4;++run) {
        source_bound_t b;agent_endpoint_t e;start(&e);
        assert(!source_bound_init(&b,100));b.end_grace_ms=AGENT_EP_PENDING_MS;
        unsigned begin=at+1000;
        for(unsigned ms=0;!b.target_samples;ms+=20) {
            if(ms==at)assert(agent_endpoint_transcribed(&e));
            agent_endpoint_hint(&e,pending,0);
            bool speech=ms<120 || (ms>=begin && ms<begin+run*20);
            assert(!source_bound_feed(&b,speech?600:80));feed(&e,1,speech);
        }
        assert(e.state>=AGENT_EP_DONE && e.elapsed_ms<=b.elapsed_ms && e.held_frames<=50);
    }
    source_bound_t b;assert(!source_bound_init(&b,100));b.end_grace_ms=1000;
    while(!b.target_samples)assert(!source_bound_feed(&b,80));
    assert(b.elapsed_ms==4000); /* Silence still gets the original4s bound. */
    assert(!source_bound_init(&b,100));
    while(!b.target_samples)assert(!source_bound_feed(&b,b.elapsed_ms<120?600:80));
    assert(b.elapsed_ms==4000); /* Classic acquisition is unchanged. */
}
static void transcript(agent_endpoint_t *e,const char *words,bool settled)
{
    unsigned notice=agent_endpoint_notice(e->asr_notice,agent_speech_meaningful_partial(words),
        agent_speech_pending_argument(words),!words[strspn(words," \t\r\n")],settled);
    agent_endpoint_observe(e,notice);
}
static void provisional_completion(void)
{
    /* A complete current draft releases semantic waiting, while a syllable
     * at the silence boundary still gets the bounded four-vote confirmation. */
    agent_endpoint_t e;start(&e);feed(&e,20,true);
    transcript(&e,"请把灯调成",false);feed(&e,34,false);
    transcript(&e,"请把灯调成蓝色。",false);
    assert(e.state==AGENT_EP_SPEECH && !e.pending && !e.held_frames);
    feed(&e,4,true);assert(e.state==AGENT_EP_SPEECH && !e.quiet_ms);
    transcript(&e,"请把灯调成蓝色，不对，改成绿色。",true);
    assert(!e.pending);feed(&e,35,false);
    assert(e.state==AGENT_EP_DONE && e.elapsed_ms==1860 && !e.held_frames && e.resume_frames==3);
    agent_endpoint_t terminal=e;transcript(&e,"请把灯调成",false);feed(&e,100,true);
    assert(!memcmp(&e,&terminal,sizeof(e)));

    /* Neither a provisional nor settled completion can bypass local silence. */
    for(unsigned settled=0;settled<2;++settled) {
        start(&e);feed(&e,20,true);transcript(&e,"请把灯调成",false);feed(&e,34,false);
        transcript(&e,"请把灯调成蓝色。",settled!=0);
        assert(e.state==AGENT_EP_SPEECH && !e.pending);feed(&e,1,false);
        assert(e.state==AGENT_EP_DONE && e.elapsed_ms==1100 && !e.held_frames);
    }
    start(&e);feed(&e,20,true);transcript(&e,"请把灯调成",false);feed(&e,39,false);
    assert(e.held_frames==5);transcript(&e,"你是谁？",true);feed(&e,1,false);
    assert(e.state==AGENT_EP_DONE && e.elapsed_ms==1200 && e.held_frames==5);

    /* Complete first drafts and ordinary greetings never acquire a new hold
     * merely because cloud finals often arrive after capture has ended. */
    const char *plain[]={"你好","你是谁？","把灯调成蓝色。"};
    for(unsigned i=0;i<sizeof(plain)/sizeof(*plain);++i) {
        start(&e);feed(&e,20,true);transcript(&e,plain[i],false);feed(&e,35,false);
        assert(e.state==AGENT_EP_DONE && e.elapsed_ms==1100 && !e.held_frames);
    }
    /* Repeated incomplete drafts cannot renew the existing quota. */
    start(&e);feed(&e,20,true);transcript(&e,"请把灯调成",false);
    while(e.state<AGENT_EP_DONE) {
        transcript(&e,e.elapsed_ms%40?"请把灯调成":"不对，改成",false);
        feed(&e,1,false);
    }
    assert(e.elapsed_ms==2100 && e.held_frames==50);
    start(&e);feed(&e,6,true);transcript(&e,"请把灯调成",false);feed(&e,35,false);
    assert(e.state==AGENT_EP_SPEECH && e.held_frames==1 && !e.local_onset);
    transcript(&e,"",false);assert(e.state==AGENT_EP_WAIT && !e.pending && e.asr_floor_ms==120);
    transcript(&e,"请把灯调成蓝色",false);feed(&e,200,false);
    assert(e.state==AGENT_EP_NO_SPEECH && e.elapsed_ms==4000 && e.held_frames==1);
    start(&e);feed(&e,20,true);transcript(&e,"请把灯调成",false);
    agent_endpoint_cancel(&e);terminal=e;transcript(&e,"请把灯调成蓝色",true);
    feed(&e,100,false);assert(!memcmp(&e,&terminal,sizeof(e)));
}
static void producer_bounds(void)
{
    source_bound_t b;agent_endpoint_t e;
    for(unsigned end=7;end<=499;++end) {
        start(&e);agent_endpoint_hint(&e,true,0);
        assert(!source_bound_init(&b,100));b.end_grace_ms=AGENT_EP_PENDING_MS;
        for(unsigned frame=0;!b.target_samples;++frame) {
            bool speech=frame<end;
            assert(!source_bound_feed(&b,speech?600:80));feed(&e,1,speech);
        }
        assert(e.state>=AGENT_EP_DONE && e.elapsed_ms<=b.elapsed_ms);
    }
    for(unsigned seed=1;seed<=128;++seed) {
        uint32_t rng=seed;start(&e);agent_endpoint_hint(&e,true,0);
        assert(!source_bound_init(&b,100));b.end_grace_ms=AGENT_EP_PENDING_MS;
        for(unsigned i=0;!b.target_samples;++i) {
            rng=rng*1664525u+1013904223u;
            bool possible=i<seed+100 && (rng&255)>25;
            bool weak=possible && ((rng>>8)&255)>40;
            bool strong=weak && ((rng>>16)&255)>70;
            assert(!source_bound_feed(&b,possible?600:80));
            if(i==140)(void)agent_endpoint_transcribed(&e);
            agent_endpoint_feed_supported(&e,strong,weak);
        }
        assert(e.state>=AGENT_EP_DONE && e.elapsed_ms<=b.elapsed_ms && e.held_frames<=50);
    }
    assert(!source_bound_init(&b,100));b.end_grace_ms=1001;
    assert(source_bound_feed(&b,600)==AGENT_ERR_ARGUMENT);
}
int main(void)
{ vocabulary();sentence_pause();memory_pause();cumulative();late_resume();cloud_proposals();hard_limits();fresh_admission();late_producer_bounds();provisional_completion();producer_bounds();puts("cumulative endpoint invariants passed"); }
