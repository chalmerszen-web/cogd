#include "candidate.h"
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>

static char candidate_source[AGENT_CANDIDATE_SOURCE_BYTES];

static void nominate(agent_candidate_gate_t *g,const char *text)
{
    agent_candidate_gate_init(g,candidate_source);agent_candidate_revision(g,1);
    assert(agent_candidate_preview(g,text));assert(atomic_load(&g->request));
}
static void *observe(void *ctx)
{
    agent_candidate_gate_t *g=ctx;unsigned request;
    while(!(request=atomic_load_explicit(&g->request,memory_order_acquire))) {}
    assert(!strcmp(agent_candidate_prompt(request),"请用一句话介绍你自己。"));
    while(!atomic_load_explicit(&g->cancelled,memory_order_acquire)) {}
    assert(atomic_load(&g->request)==request);return NULL;
}
static const char question[]="我刚给这盏灯取的名字是什么？";
static void *observe_source(void *ctx)
{
    agent_candidate_gate_t *g=ctx;
    while(!atomic_load_explicit(&g->request,memory_order_acquire)) {}
    assert(!strcmp(agent_candidate_text(g),"待续：我刚给这盏灯取的名字是什么？"));
    assert(agent_candidate_reply(g,"嗯，灯取的名字，我想想。"));
    return NULL;
}
static void contextual(void)
{
    agent_candidate_gate_t g;nominate(&g,"我刚给这盏灯");
    assert((atomic_load(&g.request)&AGENT_CANDIDATE_KIND)==AGENT_GUESS_THINK);
    assert(!strcmp(agent_candidate_text(&g),"待续：我刚给这盏灯"));
    assert(agent_candidate_reply(&g,"嗯，这盏灯，我想想。"));
    assert(!agent_candidate_reply(&g,"嗯，灯的名字，我想想。")); /* Not in this preview yet. */
    assert(!agent_candidate_reply(&g,"嗯，星星，我想想。"));
    assert(!agent_candidate_reply(&g,"嗯，这盏灯，我諗諗先。"));
    const char *bad[]={"嗯，灯，我想想。","嗯，这盏灯，已经好了。","小星星。",
        "嗯，这盏灯，我想想。顺便开灯。","嗯，这盏灯，我想想\"。",
        "嗯，这盏灯，我想想。{tool}","嗯，\xe4\xb8，我想想。",
        "嗯，1234567890123456789，我想想。","嗯，{这盏灯}，我想想。"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)
        assert(!agent_candidate_reply(&g,bad[i]));
    agent_candidate_revision(&g,2);
    assert(!g.revoked && !agent_candidate_preview(&g,question));
    assert(!strcmp(agent_candidate_text(&g),"待续：我刚给这盏灯")); /* Immutable publication. */
    assert(agent_candidate_final(&g,question,true));
    assert(agent_candidate_final(&g,"我刚，给这盏灯。\n取的名字是什么？",true));
    assert(!agent_candidate_final(&g,question,false));
    assert(!agent_candidate_final(&g,"我刚给这盏",true));
    assert(!agent_candidate_final(&g,"我刚给另一盏灯取的名字是什么？",true));
    assert(!agent_candidate_final(&g,"我刚给这盏灯，取消。",true));
    assert(!agent_candidate_final(&g,"我刚给这盏灯，不对，问你明天天气。",true));
    assert(!agent_candidate_final(&g,"我刚给这盏灯，用粤语回答。",true));
    assert(!agent_candidate_preview(&g,"不对，问你明天天气。") && g.revoked);
    assert(!agent_candidate_final(&g,question,true));
    nominate(&g,"我想知道呢盞燈嘅名字");
    assert(agent_candidate_reply(&g,"嗯，呢盞燈，我諗諗先。"));
    assert(!agent_candidate_reply(&g,"嗯，呢盞燈，我想想。"));
    assert(agent_candidate_final(&g,"我想知道呢盞燈嘅名字係乜嘢？",true));
    /* Actual provider diagnosis shortened the noun phrase by deleting 是.
     * Ordered source characters remain grounded; new/reordered ones do not. */
    nominate(&g,"我想知道为什么天空是蓝色的？");
    assert(agent_candidate_reply(&g,"嗯，天空蓝色，我想想。"));
    assert(!agent_candidate_reply(&g,"嗯，蓝色天空，我想想。"));
    assert(!agent_candidate_reply(&g,"嗯，天空云朵，我想想。"));
    assert(!agent_candidate_reply(&g,"嗯，我来调整一下灯光。"));
    agent_candidate_gate_init(&g,candidate_source);agent_candidate_revision(&g,1);
    assert(!agent_candidate_preview(&g,"嗯，这个"));
    assert(!agent_candidate_preview(&g,"abcdef"));
    assert(!agent_candidate_preview(&g,"abcdef\xe4\xb8"));
    char large[258];memset(large,'a',257);large[257]=0;
    assert(agent_candidate_preview(&g,large));
    assert(strlen(agent_candidate_text(&g))==64);
    assert(agent_candidate_final(&g,large,true));
    nominate(&g,"为什么这盏灯为什么这盏灯为什么这盏灯为什么这盏灯为什么这盏灯");
    assert(strlen(agent_candidate_text(&g))==63);
    assert(agent_candidate_final(&g,"为什么这盏灯为什么这盏灯为什么这盏灯为什么这盏灯为什么这盏灯",true));
    for(unsigned i=0;i<100;++i) {
        agent_candidate_gate_init(&g,candidate_source);pthread_t task;
        assert(!pthread_create(&task,NULL,observe_source,&g));
        agent_candidate_revision(&g,1);assert(agent_candidate_preview(&g,question));
        assert(!pthread_join(task,NULL));
    }
}
static void request_scaffolding(void)
{
    agent_candidate_gate_t g;agent_candidate_gate_init(&g,candidate_source);
    agent_candidate_revision(&g,1);
    const char *early[]={"请","请用","请用一句","请用一句话","请用一句话介"};
    for(unsigned i=0;i<sizeof(early)/sizeof(*early);++i) {
        assert(!agent_candidate_preview(&g,early[i]));
        assert(!atomic_load(&g.request) && !g.revoked && !candidate_source[0]);
    }
    assert(agent_candidate_preview(&g,"请用一句话介绍"));
    assert((atomic_load(&g.request)&AGENT_CANDIDATE_KIND)==AGENT_GUESS_SELF);
    assert(!agent_candidate_preview(&g,"请用一句话介绍你自己。") && !g.revoked);
    assert(agent_candidate_final(&g,"请用一句话介绍你自己。\n",true));
    /* No prediction of the object from the scaffolding or the verb alone. */
    assert(!agent_candidate_final(&g,"请用一句话介绍天文学。",true));
    assert(!agent_candidate_final(&g,"请用一句话介绍自己，然后开灯。",true));
    assert(!agent_candidate_final(&g,"请用一句话介绍你自己。",false));
    assert(!agent_candidate_preview(&g,"算了，不用介绍。") && g.revoked);
    const char *empty_topics[]={"請用一句話簡單介","请，简单简单介","唔該，簡單簡單講","请用一句话解释"};
    for(unsigned i=0;i<sizeof(empty_topics)/sizeof(*empty_topics);++i) {
        agent_candidate_gate_init(&g,candidate_source);agent_candidate_revision(&g,1);
        assert(!agent_candidate_preview(&g,empty_topics[i]) && !atomic_load(&g.request));
    }
    /* A genuinely informative generic topic still gets a revocable receipt. */
    nominate(&g,"请用一句话解释彩虹形成的原因");
    assert((atomic_load(&g.request)&AGENT_CANDIDATE_KIND)==AGENT_GUESS_THINK);
    assert(agent_candidate_final(&g,"请用一句话解释彩虹形成的原因。",true));
    assert(!agent_candidate_final(&g,"请用一句话解释地震形成的原因。",true));
}
static void second_person_intro(void)
{
    /* Actual UX138 recognition revisions previously consumed THINK. The
     * provisional "不用你" is superseded before any request is nominated. */
    const char *early[]={"不用","不用你","不用你。","你","你用","你用一句话",
                         "你用一句话。","你用一句话介"};
    agent_candidate_gate_t g;agent_candidate_gate_init(&g,candidate_source);
    agent_candidate_revision(&g,1);
    for(unsigned i=0;i<sizeof(early)/sizeof(*early);++i) {
        assert(!agent_candidate_preview(&g,early[i]));
        assert(!atomic_load(&g.request) && !g.revoked);
    }
    assert(agent_candidate_preview(&g,"你用一句话介绍"));
    assert((atomic_load(&g.request)&AGENT_CANDIDATE_KIND)==AGENT_GUESS_SELF);
    assert(!agent_candidate_preview(&g,"你用一句话介绍你自己。") && !g.revoked);
    assert(agent_candidate_final(&g,"你用一句话介绍你自己。\n",true));
    assert(agent_candidate_reply(&g,"我是ESP-HI助手小言。"));
    assert(!agent_candidate_final(&g,"你用一句话介绍你自己。",false));
    const char *rejected[]={"你用一句话介绍天文学。","你用一句话介绍你自己和李白。",
        "你用一句话介绍你自己，然后开灯。","你用一句话介绍你自己。不对，讲个笑话。",
        "你不要介绍你自己。","你不用介绍你自己。","你们介绍你自己。"};
    for(unsigned i=0;i<sizeof(rejected)/sizeof(*rejected);++i)
        assert(!agent_candidate_final(&g,rejected[i],true));
    assert(!agent_candidate_preview(&g,"你不用介绍了。") && g.revoked);
    assert(!agent_candidate_final(&g,"你用一句话介绍你自己。",true));
    const char *empty[]={"您用一句话介","請您用一句話簡單介","请你用一句话解释"};
    for(unsigned i=0;i<sizeof(empty)/sizeof(*empty);++i) {
        agent_candidate_gate_init(&g,candidate_source);agent_candidate_revision(&g,1);
        assert(!agent_candidate_preview(&g,empty[i]) && !atomic_load(&g.request));
    }
    nominate(&g,"您用一句话简单介绍一下");
    assert((atomic_load(&g.request)&AGENT_CANDIDATE_KIND)==AGENT_GUESS_SELF);
    assert(agent_candidate_final(&g,"您用一句话简单介绍一下自己。",true));
    agent_candidate_revision(&g,2);
    assert(g.revoked && !agent_candidate_final(&g,"您用一句话简单介绍一下自己。",true));
    nominate(&g,"你用一句话解释彩虹形成的原因");
    assert((atomic_load(&g.request)&AGENT_CANDIDATE_KIND)==AGENT_GUESS_THINK);
    assert(agent_candidate_final(&g,"你用一句话解释彩虹形成的原因。",true));
    assert(!agent_candidate_final(&g,"你用一句话解释地震形成的原因。",true));
}
static void quick_answers(void)
{
    agent_candidate_gate_t g;
    agent_candidate_gate_init(&g,candidate_source);agent_candidate_revision(&g,1);
    assert(!agent_candidate_preview(&g,"为什么天空"));
    assert(!agent_candidate_preview(&g,"为什么天空？"));
    assert(!agent_candidate_preview(&g,"为什么天空是"));
    assert(!agent_candidate_preview(&g,"为什么天空是蓝色的"));
    assert(agent_candidate_preview(&g,"为什么天空是蓝色的？"));
    assert((atomic_load(&g.request)&AGENT_CANDIDATE_KIND)==AGENT_GUESS_ANSWER);
    assert(!strcmp(agent_candidate_text(&g),"快答：为什么天空是蓝色的？"));
    assert(agent_candidate_reply(&g,"因为蓝光更容易被空气散射。"));
    assert(!agent_candidate_reply(&g,"嗯，天空蓝色，我想想。"));
    assert(!agent_candidate_reply(&g,"因为蓝光更容易散射。然后呢。"));
    assert(!agent_candidate_reply(&g,"已经把灯改成蓝色了。"));
    agent_candidate_revision(&g,2);
    assert(!g.revoked && agent_candidate_final(&g,"  为什么天空是蓝色的？\n",true));
    assert(agent_candidate_final(&g,"为什么天空是蓝色的。",true));
    assert(!agent_candidate_final(&g,"为什么天空是蓝色的？",false));
    const char *changed[]={"为什么天空是红色的？","为什么天空是蓝色的？请详细解释。",
        "为什么天空是蓝色的？用粤语回答。","为什么天空是蓝色的？不对，是大海。",
        "为什么天空是蓝色的？再把灯设为红色。","为什么天空，是蓝色的？","为什么天空是蓝色的？取消。"};
    for(unsigned i=0;i<sizeof(changed)/sizeof(*changed);++i)assert(!agent_candidate_final(&g,changed[i],true));
    nominate(&g,"什么是圆周率3.14？");
    assert(!agent_candidate_final(&g,"什么是圆周率314？",true));
    assert(agent_candidate_final(&g,"什么是圆周率3.14？",true));
    nominate(&g,"乜嘢係彩虹？");
    assert((atomic_load(&g.request)&AGENT_CANDIDATE_KIND)==AGENT_GUESS_ANSWER);
    assert(atomic_load(&g.request)&AGENT_CANDIDATE_YUE);
    assert(agent_candidate_final(&g,"乜嘢係彩虹？",true));
    nominate(&g,"什么是彩虹？");
    assert(agent_candidate_final(&g,"什么是彩虹？",true));
    assert(!agent_candidate_preview(&g,"不对，什么是极光？") && g.revoked);
    const char *fallback[]={"为什么现在灯不亮？","为什么今天下雨？","为什么之前没有记住？",
        "为什么屏幕没有显示时间？","为什么天空是蓝色的再播放音乐？"};
    for(unsigned i=0;i<sizeof(fallback)/sizeof(*fallback);++i) {
        nominate(&g,fallback[i]);
        assert((atomic_load(&g.request)&AGENT_CANDIDATE_KIND)==AGENT_GUESS_THINK);
    }
}
static void connective_receipts(void)
{
    agent_candidate_gate_t g;nominate(&g,"现在灯是什么颜色");
    /* Recorded193/199transcripts: eight source characters fit the bounded
     * receipt. A possessive connector still cannot introduce a new fact. */
    const char *recorded[]={"嗯，现在灯是白色，我想想。","嗯，现在灯，我想想。",
        "嗯，灯的颜色，我想想。","嗯，现在灯是什么颜色，我想想。","嗯，灯的颜色，我想想。"};
    const bool accepted[]={false,true,true,true,true};
    for(unsigned i=0;i<sizeof(recorded)/sizeof(*recorded);++i)
        assert(agent_candidate_reply(&g,recorded[i])==accepted[i]);
    const char *bad[]={"嗯，白色的灯，我想想。","嗯，灯已经亮，我想想。",
        "嗯，灯不是白色，我想想。","嗯，颜色的灯，我想想。",
        "嗯，的灯颜色，我想想。","嗯，灯颜色的，我想想。",
        "嗯，灯的的颜色，我想想。","嗯，灯嘅颜色，我想想。",
        "嗯，灯的，我想想。","嗯，的的，我想想。","嗯，灯的名字，我想想。"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(!agent_candidate_reply(&g,bad[i]));
    assert(!agent_speech_thinking_topic("嗯，现在灯光是什么颜色，我想想。",NULL)); /*9Han*/
    assert(!agent_speech_thinking_topic("嗯,abcdefghi,我想想。",NULL)); /*9ASCII*/
    assert(agent_speech_thinking_topic("嗯,abcdefgh,我想想",NULL));
    assert(agent_candidate_final(&g,"现在灯是什么颜色？请简单回答。",true));
    assert(!agent_candidate_final(&g,"不用问灯了，请介绍你自己。",true));
    assert(!agent_candidate_final(&g,"现在灯是什么颜色？",false));
    assert(!agent_candidate_preview(&g,"不对，问一下天气。") && g.revoked);
    assert(!agent_candidate_final(&g,"现在灯是什么颜色？",true));
    nominate(&g,"而家燈係乜嘢顏色");
    assert(atomic_load(&g.request)&AGENT_CANDIDATE_YUE);
    assert(agent_candidate_reply(&g,"嗯，燈嘅顏色，我諗諗先。"));
    assert(!agent_candidate_reply(&g,"嗯，燈的顏色，我諗諗先。"));
    assert(!agent_candidate_reply(&g,"嗯，燈嘅白色，我諗諗先。"));
    assert(!agent_candidate_reply(&g,"嗯，嘅燈顏色，我諗諗先。"));
    assert(!agent_candidate_reply(&g,"嗯，燈嘅嘅顏色，我諗諗先。"));
    assert(!agent_candidate_reply(&g,"嗯，燈嘅顏色，我想想。"));
    puts("Recorded receipts:3/5admissible vs1/5before; original facts/order, bounded particle and final authority preserved");
}
int main(void)
{
    connective_receipts();
    agent_candidate_gate_t g;
    nominate(&g,"请用一句话介绍");
    assert(!agent_candidate_final(&g,"请用一句话介绍你自己。",false));
    assert(agent_candidate_final(&g,"请用一句话介绍你自己。",true));
    assert(!agent_candidate_final(&g,"介绍这个设备的硬件",true));
    assert(!agent_candidate_preview(&g,"介绍你自己。"));
    agent_candidate_revision(&g,2);
    assert(!agent_candidate_final(&g,"请用一句话介绍你自己。",true));
    assert(g.revoked && atomic_load(&g.cancelled));
    assert(!agent_candidate_preview(&g,"请把灯设为红色"));
    nominate(&g,"请把灯");
    assert(agent_candidate_final(&g,"请把灯设为蓝色，请简单回答。",true));
    assert(!agent_candidate_preview(&g,"请把灯设为蓝色，不对，改成绿色。"));
    agent_candidate_revision(&g,2);
    assert(!g.revoked && !atomic_load(&g.cancelled));
    assert(agent_candidate_final(&g,"请把灯设为蓝色，不对，改成绿色。",true));
    assert(agent_candidate_final(&g,"请把灯调成蓝色。不对，不要蓝色，改成绿色。",true));
    assert(agent_candidate_final(&g,"请把灯调成蓝色。不对，不要蓝色，改成绿色。\n",true));
    assert(agent_candidate_final(&g,"请把灯设为蓝色。\r\n\t ",true));
    assert(!agent_candidate_final(&g,"请把灯设为蓝色。\n播放音乐。",true));
    assert(!agent_candidate_final(&g,"请把灯调成蓝色。不对，不要蓝色，改成绿色。",false));
    const char *rejected[]={"不对，改成绿色。","请把灯设为蓝色，不对，改成",
        "请把灯设为蓝色，不对，不要绿色，改成绿色。", "请把灯设为蓝色吗？",
        "请把灯设为蓝色，不对，取消。", "请把灯设为蓝色，不对，改成绿色，再播放音乐。",
        "请把灯设为蓝色，不是改成绿色。", "唔該將燈改做綠色。"};
    for(unsigned i=0;i<sizeof(rejected)/sizeof(*rejected);++i)
        assert(!agent_candidate_final(&g,rejected[i],true));
    /* Explicit cancellation and a different task stay irrevocable even if a
     * later transcript returns to the old category. No second nomination. */
    assert(!agent_candidate_preview(&g,"算了，取消。"));
    assert(g.revoked && !agent_candidate_final(&g,"请把灯设为绿色。",true));
    nominate(&g,"请把灯");agent_candidate_revision(&g,2);
    assert(!agent_candidate_preview(&g,"介绍你自己。"));
    assert(g.revoked && !agent_candidate_final(&g,"请把灯设为绿色。",true));
    nominate(&g,"唔該將燈");agent_candidate_revision(&g,2);
    assert(!agent_candidate_preview(&g,"唔係，改做綠色。"));
    assert(agent_candidate_final(&g,"唔該將燈改做藍色。唔係，唔好藍色，改做綠色。",true));
    assert(agent_candidate_final(&g,"唔該將燈改做藍色。唔係，唔好藍色，改做綠色。\n",true));
    assert(!agent_candidate_final(&g,"请把灯调成蓝色，不对，改成绿色。",true));
    nominate(&g,"记住");
    assert(!agent_candidate_final(&g,"记住",true));
    assert(agent_candidate_final(&g,"记住我的名字是小明",true));
    assert(!agent_candidate_final(&g,"记住，不，取消",true));
    unsigned memory_request=atomic_load(&g.request);
    agent_candidate_revision(&g,2);
    assert(!agent_candidate_preview(&g,"记住。\n我的名字是小明。\n"));
    assert(!g.revoked && atomic_load(&g.request)==memory_request);
    assert(agent_candidate_final(&g,"记住。\n我的名字是小明。\n",true));
    assert(!agent_candidate_final(&g,"记住。\n我的名字是小明。\n",false));
    assert(!agent_candidate_final(&g,"我的名字是小明。",true));
    assert(!agent_candidate_final(&g,"记住我的名字了吗？",true));
    assert(!agent_candidate_final(&g,"記低我個名。",true));
    assert(!agent_candidate_preview(&g,"记住。\n算了，取消。"));
    assert(g.revoked && !agent_candidate_final(&g,"记住我的名字是小明。",true));
    nominate(&g,"记住");agent_candidate_revision(&g,2);
    assert(!agent_candidate_preview(&g,"不对，先不要记。"));
    assert(g.revoked);
    nominate(&g,"记住");agent_candidate_revision(&g,2);
    assert(!agent_candidate_preview(&g,"介绍你自己。"));assert(g.revoked);
    nominate(&g,"唔該記低");agent_candidate_revision(&g,2);
    assert(!agent_candidate_preview(&g,"唔該記低。\n盞燈叫小星星。"));
    assert(agent_candidate_final(&g,"唔該記低。\n盞燈叫小星星。",true));
    assert(!agent_candidate_final(&g,"唔該記低。",true));
    assert(!agent_candidate_preview(&g,"唔使記低。"));assert(g.revoked);
    nominate(&g,"你係邊個？");
    assert(atomic_load(&g.request)&AGENT_CANDIDATE_YUE);
    assert(agent_candidate_final(&g,"你係邊個？",true));
    assert(!agent_candidate_final(&g,"你是谁？",true));
    for(unsigned i=0;i<100;++i) {
        agent_candidate_gate_init(&g,candidate_source);pthread_t task;
        assert(!pthread_create(&task,NULL,observe,&g));
        agent_candidate_revision(&g,1);assert(agent_candidate_preview(&g,"介绍"));
        agent_candidate_revision(&g,2);assert(!pthread_join(task,NULL));
    }
    contextual();quick_answers();request_scaffolding();second_person_intro();
    puts("Candidate: source-bound contextual receipts, immutable publication, final-only admission, cancellation and language OK");
}
