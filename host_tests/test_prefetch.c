#include "prefetch.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    agent_realtime_t q;
    agent_speech_t speech;
    agent_prefetch_t p;
    atomic_bool cancelled;
    uint64_t now;
    unsigned closes,seed;
    char scratch[AGENT_RT_SCRATCH],input[2049];
    uint32_t before[4];
    uint8_t cache[AGENT_PREFETCH_BYTES];
    uint32_t after[4];
} state;
static uint64_t clock_ms(void *ctx) { (void)ctx;return state.now; }
static void close_ws(void *ctx) { (void)ctx;++state.closes; }
static agent_err_t open_pcm(void *ctx,unsigned rate) { (void)ctx;assert(rate==16000);return AGENT_OK; }
static agent_err_t write_pcm(void *ctx,const int16_t *pcm,size_t n)
{ (void)ctx;return agent_prefetch_pcm(&state.p,pcm,n); }
static agent_err_t receive(void *ctx,const cJSON *root)
{ (void)ctx;return agent_prefetch_event(&state.p,root); }
static const agent_ws_ops_t ws={.close=close_ws};
static void init(void)
{
    memset(&state,0,sizeof(state));atomic_init(&state.cancelled,false);state.now=1;state.seed=17;
    state.speech=(agent_speech_t){.scratch=state.scratch,.capacity=sizeof(state.scratch),
        .now_ms=clock_ms,.cancelled=&state.cancelled};
    const agent_pcm_sink_t sink={open_pcm,write_pcm,NULL};
    agent_realtime_init(&state.q,&state.speech,&ws,&sink,receive,NULL);
    state.q.opened=state.q.created=state.q.ready=state.q.manual_draft=true;
    state.q.deadline=45000;state.q.input_samples=16000*4;
    memset(state.before,0xa5,sizeof(state.before));memset(state.after,0xa5,sizeof(state.after));
    agent_prefetch_init(&state.p,&state.q,state.cache,sizeof(state.cache),state.input,sizeof(state.input));
}
static agent_err_t wire(const char *json)
{
    size_t at=0,length=strlen(json);
    while(at<length) {
        state.seed=state.seed*1664525u+1013904223u;
        size_t n=1+state.seed%127;if(n>length-at)n=length-at;
        agent_ws_chunk_t chunk={n,at?0:1,true,at+n==length};
        agent_err_t error=agent_realtime_receive(&state.q,json+at,&chunk);
        if(error)return error;at+=n;
    }
    return AGENT_OK;
}

static void draft(void)
{
    agent_prefetch_preview(&state.p,"请介绍");
    assert(agent_prefetch_prepare(&state.p,16000));
    assert(!agent_prefetch_prepare(&state.p,16000));
    state.p.requested=state.q.pending=true;
    assert(!wire("{\"type\":\"response.created\",\"response\":{\"id\":\"draft1\"}}"));
}
static void answer(void)
{
    assert(!wire("{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"draft1\",\"delta\":\"我是小言。\"}"));
    assert(!wire("{\"type\":\"response.audio.delta\",\"response_id\":\"draft1\",\"delta\":\"AAABAP//AEAAwA==\"}"));
    assert(!wire("{\"type\":\"response.done\",\"response\":{\"id\":\"draft1\",\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"audio\",\"transcript\":\"我是小言。\"}]}]}}"));
}
static agent_err_t final(const char *id,const char *text)
{
    char wiretext[1024];agent_json_writer_t w;agent_json_writer_init(&w,wiretext,sizeof(wiretext));
    agent_json_raw(&w,"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":");
    agent_json_quote(&w,id);agent_json_raw(&w,",\"transcript\":");agent_json_quote(&w,text);agent_json_raw(&w,"}");
    assert(!w.error);return wire(wiretext);
}
static void commit(void)
{
    state.p.commit_sent=true;
    assert(!wire("{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"complete_input\"}"));
}
static void identities(void)
{
    init();draft();answer();assert(!agent_prefetch_ready(&state.p));
    assert(agent_prefetch_output_ready(&state.p) && !agent_prefetch_input_ready(&state.p));
    /* Preview identities are advisory and may change after response.create. */
    assert(!wire("{\"type\":\"conversation.item.input_audio_transcription.delta\",\"item_id\":\"preview_old\",\"text\":\"你好\",\"stash\":\"小言\"}"));
    commit();assert(!final("complete_input","请介绍你自己。"));
    assert(agent_prefetch_input_ready(&state.p) && agent_prefetch_ready(&state.p));
    assert(state.p.audio.samples==5 && !state.p.discarded);
    assert(!final("complete_input","请介绍你自己。"));
    assert(final("complete_input","不要请介绍你自己。")==AGENT_ERR_PROTOCOL);
    assert(!agent_prefetch_ready(&state.p));

    init();draft();answer();agent_prefetch_preview(&state.p,"请记住");
    agent_prefetch_preview(&state.p,"请介绍"); /* Topic restoration cannot un-revoke. */
    commit();assert(!final("complete_input","请介绍你自己。"));
    assert(state.p.invalid && state.p.discarded==1 && !state.p.audio.samples && !agent_prefetch_ready(&state.p));

    init();draft();answer();commit();assert(final("preview_old","请介绍你自己。")==AGENT_ERR_PROTOCOL);
    init();draft();answer();assert(final("complete_input","请介绍你自己。")==AGENT_ERR_PROTOCOL);
    init();draft();answer();commit();assert(!final("complete_input",""));
    assert(state.p.incomplete_input && !agent_prefetch_input_ready(&state.p));
    init();draft();answer();commit();assert(!final("complete_input","请介绍你自己。"));
    state.q.pending=true;
    assert(wire("{\"type\":\"response.created\",\"response\":{\"id\":\"draft1\"}}")==AGENT_ERR_PROTOCOL);
    init();draft();atomic_store(&state.cancelled,true);
    assert(wire("{\"type\":\"response.audio.delta\",\"response_id\":\"draft1\",\"delta\":\"AAA=\"}")==AGENT_ERR_CANCELLED);
    assert(!state.p.audio.samples);
}
static void limits(void)
{
    static int16_t pcm[81921];
    init();draft();assert(!agent_prefetch_pcm(&state.p,pcm,81921));
    assert(!agent_prefetch_pcm(&state.p,pcm,1));answer();commit();assert(!final("complete_input","请介绍你自己。"));
    assert(state.p.unusable && !agent_prefetch_ready(&state.p));
    init();draft();answer();commit();assert(!final("complete_input","请介绍李白。"));
    assert(state.p.invalid && !agent_prefetch_ready(&state.p));
    init();draft();assert(!wire("{\"type\":\"response.output_item.added\",\"response_id\":\"draft1\",\"item\":{\"type\":\"function_call\"}}"));
    answer();commit();assert(!final("complete_input","请介绍你自己。"));assert(!agent_prefetch_ready(&state.p));
}
static void intent_cases(void)
{
    static const struct {const char *text;agent_speech_guess_t final;} cases[]={
        {"请用一句话介绍你自己。",AGENT_GUESS_SELF},
        {"你用一句话介绍你自己。",AGENT_GUESS_SELF},
        {"请你简单介绍下自己。",AGENT_GUESS_SELF},
        {"請您用一句話簡單介紹一下自己。",AGENT_GUESS_SELF},
        {"您，介绍自己。",AGENT_GUESS_SELF},
        {"你 用一句话介绍你自己。",AGENT_GUESS_SELF},
        {"用一句话你是谁？",AGENT_GUESS_SELF},
        {"简单你係邊個？",AGENT_GUESS_SELF},
        {"你们介绍你自己。",AGENT_GUESS_NONE},
        {"你介绍你自己和李白。",AGENT_GUESS_NONE},
        {"你用一句话介绍你自己，然后开灯。",AGENT_GUESS_NONE},
        {"你不要介绍你自己。",AGENT_GUESS_NONE},
        {"您不用介绍你自己。",AGENT_GUESS_NONE},
        {"你介绍你自己了吗？",AGENT_GUESS_NONE},
        {"你用一句话介绍天文学。",AGENT_GUESS_NONE},
        {"唔該，介紹下你自己。",AGENT_GUESS_SELF},
        {"你係邊個？",AGENT_GUESS_SELF},
        {"请介绍李白。",AGENT_GUESS_NONE},
        {"请介绍你自己，不对，讲个笑话。",AGENT_GUESS_NONE},
        {"请介绍你自己和李白。",AGENT_GUESS_NONE},
        {"你是谁？然后帮我调灯。",AGENT_GUESS_NONE},
        {"请记住我的名字叫蓝天。",AGENT_GUESS_REMEMBER},
        {"請記低我唔鍾意紅色。",AGENT_GUESS_REMEMBER},
        {"请记住。",AGENT_GUESS_NONE},
        {"记住我的名字了吗？",AGENT_GUESS_NONE},
        {"記低我個名未?",AGENT_GUESS_NONE},
        {"请记住。\n灯的名字叫星星。\n",AGENT_GUESS_REMEMBER},
        {"请记住我的名字，算了，不用。",AGENT_GUESS_NONE},
        {"请不要记住我的名字。",AGENT_GUESS_NONE},
        {"请把灯调成蓝色。",AGENT_GUESS_LIGHT},
        {"请把灯调成蓝色。不对，不要蓝色，改成绿色。",AGENT_GUESS_LIGHT},
        {"請把燈調成藍色。不對，改成綠色。",AGENT_GUESS_LIGHT},
        {"请把灯调成蓝色？",AGENT_GUESS_NONE},
        {"请把灯调成蓝色，不要执行。",AGENT_GUESS_NONE},
        {"请把灯旁边的屏幕设为蓝色。",AGENT_GUESS_NONE},
        {"请把灯调成蓝色，还是绿色。",AGENT_GUESS_NONE},
        {"灯的原理是什么？",AGENT_GUESS_NONE},
        {"如果可以请把灯调成蓝色。",AGENT_GUESS_NONE},
        {"请把灯调成蓝色。再把音量设为绿色。",AGENT_GUESS_NONE},
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        agent_speech_guess_t result=agent_speech_guess(cases[i].text,true);
        if(result!=cases[i].final)fprintf(stderr,"intent case%u: %s got%u expected%u\n",i,cases[i].text,result,cases[i].final);
        assert(result==cases[i].final);
    }
    assert(agent_speech_guess("请介绍",false)==AGENT_GUESS_SELF);
    assert(agent_speech_guess("请把灯",false)==AGENT_GUESS_LIGHT);
    assert(agent_speech_guess("请记住",false)==AGENT_GUESS_REMEMBER);
    assert(!agent_speech_guess("请",false));
    uint8_t rgb[3]={17,18,19};
    assert(agent_speech_light_literal("請將所有燈光設置為綠色。",rgb));
    assert(!rgb[0] && rgb[1]==255 && !rgb[2]);
    assert(agent_speech_light_literal("唔該，把燈調成紫色。",rgb));
    assert(rgb[0]==255 && !rgb[1] && rgb[2]==255);
    assert(!agent_speech_light_literal("請把燈旁邊的屏幕設為藍色。",rgb));
    assert(rgb[0]==255 && !rgb[1] && rgb[2]==255);
    assert(!agent_speech_light_literal(NULL,rgb));
    assert(!agent_speech_light_literal("把灯设为蓝色。",NULL));
    static const struct { const char *text;unsigned mask; } repairs[]={
        {"请把灯调成蓝色，不对，不要蓝色，改成绿色。",2},
        {"把灯设为蓝色 不对 改为红色。",4},
        {"請把燈調成藍色。不對，改成綠色。",2},
        {"唔該，把燈調成藍色，唔係，唔好藍色，改做紫色。",5},
        {"把灯设为蓝色，不对，改成绿色，不对，改成红色，不对，改成黄色。",6},
        {"把灯设为蓝色，不对，改成绿色，不对，改成红色，不对，改成黄色，不对，改成白色。",0},
        {"把灯设为蓝色，不对，不要红色，改成绿色。",0},
        {"把灯设为蓝色，不要蓝色，改成绿色。",0},
        {"把灯设为蓝色，不对，改成绿色？",0},
        {"把灯设为蓝色，不对，改成绿色，如果可以。",0},
        {"把灯设为蓝色，不对，不要蓝色。",0},
        {"把灯设为蓝色，不对，不要蓝色，",0},
        {"把灯设为蓝色，不对，改成。",0},
        {"把灯设为蓝色，不对，取消。",0},
        {"把灯设为蓝色，不对，改成绿色，算了。",0},
        {"把灯设为蓝色，不对，改成绿色，再关掉。",0},
        {"把灯设为蓝色不对改成绿色。",0},
        {"把灯设为蓝色，不是改成绿色。",0},
        {"唔該，把燈調成藍色，唔係改做綠色。",0},
        {"把灯设为蓝色，不对改成绿色。",0},
        {"把灯设为蓝色，不是，改成绿色。",2},
        {"把灯设为蓝色，不对，改成绿色。不要执行。",0},
    };
    for(unsigned i=0;i<sizeof(repairs)/sizeof(*repairs);++i) {
        const uint8_t untouched[]={17,18,19};memcpy(rgb,untouched,sizeof(rgb));
        bool accepted=agent_speech_light_literal(repairs[i].text,rgb);
        assert(accepted==(repairs[i].mask!=0));
        if(!accepted)assert(!memcmp(rgb,untouched,sizeof(rgb)));
        else for(unsigned k=0;k<3;++k)assert(rgb[k]==((repairs[i].mask&(4u>>k))?255:0));
    }
    assert(agent_speech_guess_reply(AGENT_GUESS_SELF,"我是小言，你的设备助手。"));
    assert(!agent_speech_guess_reply(AGENT_GUESS_SELF,"我是小言，转写助手。"));
    assert(agent_speech_guess_reply(AGENT_GUESS_LIGHT,"嗯，我来调整灯光。"));
    assert(!agent_speech_guess_reply(AGENT_GUESS_LIGHT,"我来调整灯光。已经改成黑色。"));
    assert(!agent_speech_guess_reply(AGENT_GUESS_LIGHT,"我来把灯改成黑色。"));
    assert(agent_speech_guess_reply(AGENT_GUESS_REMEMBER,"噢，让我记下来。"));
    assert(agent_speech_guess_reply(AGENT_GUESS_REMEMBER,"我嚟記低先。"));
    assert(!agent_speech_guess_reply(AGENT_GUESS_REMEMBER,"我来记住小星星。"));
    init();assert(!agent_prefetch_prepare(&state.p,16000));
    agent_prefetch_preview(&state.p,"请介绍");
    assert(!agent_prefetch_prepare(&state.p,0));
    assert(!agent_prefetch_prepare(&state.p,AGENT_RT_INPUT_MAX+1));
    state.q.message=true;assert(!agent_prefetch_prepare(&state.p,16000));
    state.q.message=false;assert(agent_prefetch_prepare(&state.p,16000));
    agent_prefetch_preview(&state.p,"请介绍一下你自己。");assert(!state.p.invalid);
    agent_prefetch_preview(&state.p,"请把灯调成蓝色。");assert(state.p.invalid && state.p.discarded==1);
    agent_prefetch_preview(&state.p,"请记住");assert(state.p.discarded==1);
    init();draft();commit();assert(!final("complete_input","请介绍你自己。"));
    assert(agent_prefetch_input_ready(&state.p) && !agent_prefetch_ready(&state.p));
    answer();assert(agent_prefetch_ready(&state.p));
    agent_prefetch_preview(&state.p,"请记住");assert(agent_prefetch_ready(&state.p)); /* Late preview cannot change final. */
}
static void bounded_cache(void)
{
    static int16_t source[81921],one[81921],many[81921];
    for(unsigned i=0;i<81921;++i)source[i]=(int16_t)((i*179u)%32001-16000);
    init();agent_speech_draft_t *d=&state.p.audio;
    assert(!agent_speech_draft_write(d,source,81921));
    size_t n;assert(!agent_speech_draft_read(d,one,81921,&n) && n==81921);
    assert(one[0]==source[0] && !memcmp(state.before,state.after,sizeof(state.before)));
    agent_speech_draft_reset(d);
    for(unsigned i=0;i<81921;) {unsigned take=(i*17)%257+1;if(take>81921-i)take=81921-i;assert(!agent_speech_draft_write(d,source+i,take));i+=take;}
    for(unsigned i=0;i<81921;) {assert(!agent_speech_draft_read(d,many+i,137,&n));i+=(unsigned)n;}
    assert(!memcmp(one,many,sizeof(one)));
    agent_speech_draft_reset(d);assert(!agent_speech_draft_write(d,source,81921));
    assert(agent_speech_draft_write(d,source,1)==AGENT_ERR_FULL && d->full);
    assert(agent_speech_draft_read(d,one,1,&n)==AGENT_ERR_ARGUMENT); /* never play truncated cache */
    for(unsigned i=0;i<4;++i)assert(state.before[i]==0xa5a5a5a5 && state.after[i]==0xa5a5a5a5);
}
static void release_cases(void)
{
    init();draft();
    assert(!wire("{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"draft1\",\"delta\":\"我是小言\"}"));
    assert(!wire("{\"type\":\"response.audio.delta\",\"response_id\":\"draft1\",\"delta\":\"AAABAP//\"}"));
    assert(!agent_prefetch_ready(&state.p));
    commit();assert(!final("complete_input","请介绍你自己。"));
    assert(!agent_prefetch_ready(&state.p)); /* Final ASR alone is insufficient. */
    assert(!wire("{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"draft1\",\"delta\":\"。\"}"));
    assert(state.q.active && !state.p.complete && agent_prefetch_ready(&state.p));
    state.p.released=true;
    assert(!wire("{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"draft1\",\"delta\":\"\\n\"}"));
    assert(!wire("{\"type\":\"response.done\",\"response\":{\"id\":\"draft1\",\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"audio\",\"transcript\":\"我是小言。\\n\"}]}]}}"));
    assert(state.p.complete && agent_prefetch_ready(&state.p));
    init();draft();answer();commit();assert(!final("complete_input","请介绍你自己。"));
    state.p.released=true;
    const int16_t pcm=1;
    assert(agent_prefetch_pcm(&state.p,&pcm,1)==AGENT_ERR_PROTOCOL); /* Adapter owns the live tail. */
}

static void answer_transcript(void)
{
    for(unsigned fault=0;fault<5;++fault) {
        init();draft();state.p.guess=AGENT_GUESS_ANSWER;
        assert(!wire("{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"draft1\",\"delta\":\"空气更容易散射蓝光。\"}"));
        assert(!wire("{\"type\":\"response.audio.delta\",\"response_id\":\"draft1\",\"delta\":\"AAABAP//AEAAwA==\"}"));
        assert(agent_prefetch_output_ready(&state.p));
        const char *end="{\"type\":\"response.audio_transcript.done\",\"response_id\":\"draft1\",\"transcript\":\"空气更容易散射蓝光。\"}";
        if(fault==1)end="{\"type\":\"response.audio_transcript.done\",\"transcript\":\"空气更容易散射蓝光。\"}";
        if(fault==2)end="{\"type\":\"response.audio_transcript.done\",\"response_id\":\"old\",\"transcript\":\"空气更容易散射蓝光。\"}";
        if(fault==3)end="{\"type\":\"response.audio_transcript.done\",\"response_id\":\"draft1\",\"transcript\":\"另一个回答。\"}";
        assert(wire(end)==(fault && fault<4?AGENT_ERR_PROTOCOL:AGENT_OK));
        assert(agent_prefetch_output_ready(&state.p)==(!fault || fault==4));
        if(fault==4)assert(wire(end)==AGENT_ERR_PROTOCOL);
        if(!fault) {
            state.p.released=true;
            assert(wire("{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"draft1\",\"delta\":\"更多内容。\"}")==AGENT_ERR_PROTOCOL);
        }
    }
}
static agent_err_t text_delta(const char *text)
{
    char event[1024];agent_json_writer_t w;agent_json_writer_init(&w,event,sizeof(event));
    agent_json_raw(&w,"{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"draft1\",\"delta\":");
    agent_json_quote(&w,text);agent_json_raw(&w,"}");assert(!w.error);return wire(event);
}
static void receipt_boundary(void)
{
    const char *receipts[]={"嗯，现在灯是什么颜色，我想想","嗯，而家燈係乜嘢顏色，我諗諗先"};
    for(unsigned i=0;i<2;++i) {
        init();draft();state.p.guess=AGENT_GUESS_THINK;
        assert(!wire("{\"type\":\"response.audio.delta\",\"response_id\":\"draft1\",\"delta\":\"AAABAP//\"}"));
        size_t n=strlen(receipts[i]);
        for(size_t at=0;at<n;at+=3) {
            char ch[4]={0};memcpy(ch,receipts[i]+at,3);assert(!text_delta(ch));
            assert(agent_prefetch_output_ready(&state.p)==(at+3==n));
            assert(!agent_prefetch_input_ready(&state.p) && !agent_prefetch_ready(&state.p));
        }
        state.p.released=true;
        assert(!text_delta(" \t。\n"));assert(agent_prefetch_output_ready(&state.p));
        char event[1024];agent_json_writer_t w;agent_json_writer_init(&w,event,sizeof(event));
        agent_json_raw(&w,"{\"type\":\"response.done\",\"response\":{\"id\":\"draft1\",\"status\":\"completed\",\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"audio\",\"transcript\":");
        agent_json_quote(&w,state.p.text);agent_json_raw(&w,"}]}]}}");assert(!w.error);
        assert(!wire(event) && state.p.complete && agent_prefetch_output_ready(&state.p));
    }
    const char *tails[]={".","!","。","！","\n",",","...","。我已经开灯了","了","。\n。"};
    for(unsigned i=0;i<sizeof(tails)/sizeof(*tails);++i) {
        init();draft();state.p.guess=AGENT_GUESS_THINK;
        assert(!text_delta(receipts[0]));
        assert(!wire("{\"type\":\"response.audio.delta\",\"response_id\":\"draft1\",\"delta\":\"AAABAP//\"}"));
        assert(agent_prefetch_output_ready(&state.p));state.p.released=true;
        assert(text_delta(tails[i])==(i<5?AGENT_OK:AGENT_ERR_PROTOCOL));
        if(i<4)assert(text_delta("。")==AGENT_ERR_PROTOCOL); /* A second mark is not a new sentence. */
    }
    /* Other reply types do not inherit this THINK-only exception. */
    init();draft();state.p.guess=AGENT_GUESS_ANSWER;
    assert(!text_delta("空气更容易散射蓝光"));
    assert(!wire("{\"type\":\"response.audio.delta\",\"response_id\":\"draft1\",\"delta\":\"AAABAP//\"}"));
    assert(!agent_prefetch_output_ready(&state.p));
    init();draft();answer();state.p.released=true;
    assert(text_delta("。")==AGENT_ERR_PROTOCOL);
}
int main(void)
{
    assert(!agent_speech_meaningful_partial(NULL));
    assert(!agent_speech_meaningful_partial("嗯。"));
    assert(!agent_speech_meaningful_partial("... 12!?"));
    assert(!agent_speech_meaningful_partial("hi"));
    assert(agent_speech_meaningful_partial("请把"));
    assert(agent_speech_meaningful_partial("唔該"));
    assert(agent_speech_meaningful_partial("hello"));
    identities();limits();intent_cases();bounded_cache();release_cases();answer_transcript();receipt_boundary();
    puts("audio draft: intent nomination and compatibility, commit identity, full input, resumed speech, stale response and bounded codec OK");
    return 0;
}
