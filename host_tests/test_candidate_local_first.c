#include "candidate.h"
#include "json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char source[AGENT_CANDIDATE_SOURCE_BYTES];
static void init(agent_candidate_gate_t *g)
{ agent_candidate_gate_init(g,source);agent_candidate_revision(g,1); }
static void local_partials(void)
{
    const char *clips[]={"请把灯","请把灯调","请把灯调成","请把灯调成蓝","请把灯调成蓝色",
        "请把灯调成蓝色，不","请把灯调成蓝色，不对","请把灯调成蓝色，不对，不要",
        "请把灯调成蓝色，不对，不要蓝色，改成绿色。",
        "請將四個燈","請將四個燈調成","請將四個燈調成藍","請將四個燈調成藍色。",
        "唔該將燈改做藍色，唔係，唔好藍色，改做綠色。"};
    agent_candidate_gate_t g;init(&g);
    for(unsigned i=0;i<sizeof(clips)/sizeof(*clips);++i) {
        agent_candidate_revision(&g,i+1);
        assert(agent_speech_light_local_preview(clips[i]));
        assert(!agent_candidate_preview_local_first(&g,clips[i]));
        assert(!atomic_load(&g.request) && !g.revoked && !source[0]);
        assert(!agent_candidate_final(&g,clips[i],true));
    }
    uint8_t rgb[3]={42,43,44};
    assert(!agent_speech_light_literal("请把灯调成蓝",rgb));
    assert(rgb[0]==42 && rgb[1]==43 && rgb[2]==44);
    assert(agent_speech_light_literal(clips[8],rgb) && !rgb[0] && rgb[1]==255 && !rgb[2]);
    assert(agent_speech_light_literal(clips[13],rgb) && !rgb[0] && rgb[1]==255 && !rgb[2]);
    assert(!agent_speech_light_literal(clips[8],NULL));
    assert(!agent_speech_light_local_preview(NULL));
    init(&g);assert(!agent_candidate_preview_local_first(&g,"请把灯调成\xe8\x93"));
    assert(!atomic_load(&g.request));
}
static void complex_and_cancel(void)
{
    const char *prompts[]={"请把灯调成蓝色，再显示时间。","请把灯按音乐节奏闪烁。",
        "請將燈改做藍色，再播放音樂。","请把灯亮度随温度变化。"};
    for(unsigned i=0;i<sizeof(prompts)/sizeof(*prompts);++i) {
        agent_candidate_gate_t g;init(&g);uint8_t rgb[3]={1,2,3};
        assert(!agent_speech_light_local_preview(prompts[i]));
        assert(!agent_speech_light_literal(prompts[i],rgb));
        assert(rgb[0]==1 && rgb[1]==2 && rgb[2]==3);
        assert(agent_candidate_preview_local_first(&g,prompts[i]));
        assert((atomic_load(&g.request)&AGENT_CANDIDATE_KIND)==AGENT_GUESS_THINK);
        assert(agent_candidate_final(&g,prompts[i],true));
        assert(!agent_candidate_final(&g,prompts[i],false));
        assert(!agent_candidate_final(&g,"请把灯调成绿色。",true));
        assert(!agent_candidate_preview_local_first(&g,"取消。") && g.revoked);
        assert(!agent_candidate_final(&g,prompts[i],true));
    }
    agent_candidate_gate_t g;init(&g);
    assert(!agent_candidate_preview_local_first(&g,"请把灯调成蓝色。"));
    agent_candidate_revision(&g,2);
    assert(agent_candidate_preview_local_first(&g,"请把灯调成蓝色，再显示时间。"));
    agent_candidate_revision(&g,3);
    assert(!agent_candidate_preview_local_first(&g,"请把灯调成蓝色，再显示时间和天气。") && !g.revoked);
    assert(agent_candidate_final(&g,"请把灯调成蓝色，再显示时间和天气。",true));
    assert(!agent_candidate_preview_local_first(&g,"不对，改成录音。") && g.revoked);
    init(&g);assert(agent_candidate_preview_local_first(&g,"请用一句话介绍你自己。"));
    assert(!agent_candidate_preview_local_first(&g,"请把灯调成蓝色。") && g.revoked);
}
static void other_paths(void)
{
    const char *prompts[]={"请用一句话介绍你自己。","为什么天空是蓝色的？",
                          "我刚给这盏灯取的名字是什么？"};
    for(unsigned i=0;i<sizeof(prompts)/sizeof(*prompts);++i) {
        agent_candidate_gate_t original,selective;char a[65],b[65];
        agent_candidate_gate_init(&original,a);agent_candidate_gate_init(&selective,b);
        agent_candidate_revision(&original,1);agent_candidate_revision(&selective,1);
        assert(agent_candidate_preview(&original,prompts[i])==agent_candidate_preview_local_first(&selective,prompts[i]));
        assert(atomic_load(&original.request)==atomic_load(&selective.request));
        assert(!strcmp(agent_candidate_text(&original),agent_candidate_text(&selective)));
        assert(agent_candidate_final(&original,prompts[i],true)==agent_candidate_final(&selective,prompts[i],true));
    }
    agent_candidate_gate_t g;init(&g);
    assert(agent_candidate_preview(&g,"请把灯")); /* Old API remains unchanged. */
    assert((atomic_load(&g.request)&AGENT_CANDIDATE_KIND)==AGENT_GUESS_LIGHT);
}
static void memory_topics(void)
{
    agent_candidate_gate_t g;init(&g);
    const char *early[]={"记住","请记住。","唔該記低","请记住，我给这盏"};
    for(unsigned i=0;i<sizeof(early)/sizeof(*early);++i) {
        agent_candidate_revision(&g,i+1);
        assert(!agent_candidate_preview_local_first(&g,early[i]));
        assert(!atomic_load(&g.request) && !g.revoked && !source[0]);
    }
    assert(!agent_speech_remember_content(NULL));
    assert(!agent_speech_remember_content("把灯设为红色"));
    assert(!strcmp(agent_speech_remember_content("请记住，我给这盏灯取名"),"，我给这盏灯取名"));
    assert(agent_candidate_preview_local_first(&g,"请记住，我给这盏灯取名"));
    unsigned request=atomic_load(&g.request);
    assert(request==(AGENT_GUESS_THINK|AGENT_CANDIDATE_MEMORY));
    assert(!strcmp(agent_candidate_text(&g),"记下：我给这盏灯取名"));
    assert(agent_candidate_reply(&g,"嗯，灯取名，我想想。"));
    assert(!agent_candidate_reply(&g,"嗯，灯叫小星星，我想想。")); /* Unheard value. */
    assert(!agent_candidate_reply(&g,"嗯，我来记一下。"));
    assert(!agent_candidate_reply(&g,"已经记住小星星。"));
    char published[65];strcpy(published,source);
    agent_candidate_revision(&g,10);
    const char full[]="请记住，我给这盏灯取名叫小星星。\n";
    assert(!agent_candidate_preview_local_first(&g,full));
    assert(!g.revoked && atomic_load(&g.request)==request && !strcmp(source,published));
    assert(agent_candidate_final(&g,full,true));
    const char *changed[]={"我给这盏灯取名叫小星星。","请记住，我给另一盏灯取名叫小星星。",
        "请记住，我给这盏灯取名了吗？","请记住，我给这盏灯取名，不对，改成",
        "请记住，我给这盏灯取名，取消。","请记住，我给这盏灯取名，唔使。",
        "请记住，我给这盏灯取名，用粤语回答。","请记住，我给这盏灯"};
    for(unsigned i=0;i<sizeof(changed)/sizeof(*changed);++i)
        assert(!agent_candidate_final(&g,changed[i],true));
    assert(!agent_candidate_final(&g,full,false));
    assert(!agent_candidate_preview_local_first(&g,"不对，请介绍你自己。") && g.revoked);
    assert(!agent_candidate_final(&g,full,true));
    init(&g);
    const char yue[]="唔該記低，呢盞燈個名叫小星星。";
    assert(agent_candidate_preview_local_first(&g,yue));
    assert((atomic_load(&g.request)&AGENT_CANDIDATE_KIND)==AGENT_GUESS_THINK);
    assert(atomic_load(&g.request)&AGENT_CANDIDATE_MEMORY);
    assert(atomic_load(&g.request)&AGENT_CANDIDATE_YUE);
    assert(agent_candidate_reply(&g,"嗯，燈個名，我諗諗先。"));
    assert(!agent_candidate_reply(&g,"嗯，燈個名，我想想。"));
    agent_candidate_revision(&g,2);
    assert(!agent_candidate_preview_local_first(&g,yue) && !g.revoked);
    assert(agent_candidate_final(&g,yue,true));
    assert(!agent_candidate_final(&g,"请记住，呢盞燈個名叫小星星。",true));
    assert(!agent_candidate_preview_local_first(&g,"唔使記低。") && g.revoked);
    init(&g);
    char large[240];strcpy(large,"请记住，");
    for(unsigned i=0;i<20;++i)strcat(large,"灯名字");
    assert(agent_candidate_preview_local_first(&g,large));
    assert(strlen(source)<=64 && agent_utf8_valid(source,strlen(source)));
    assert(agent_candidate_final(&g,large,true));
    init(&g);
    assert(!agent_candidate_preview_local_first(&g,"请记住这盏灯\xe5\x90"));
    assert(!atomic_load(&g.request));
    agent_candidate_gate_init(&g,NULL);agent_candidate_revision(&g,1);
    assert(!agent_candidate_preview_local_first(&g,full) && !atomic_load(&g.request));
    init(&g);assert(agent_candidate_preview(&g,"记住")); /* Original policy. */
    assert(atomic_load(&g.request)==AGENT_GUESS_REMEMBER);
}
static void early_memory_predictions(void)
{
    agent_candidate_gate_t g;init(&g);
    assert(!agent_candidate_preview_local_first(&g,"请记住，我给这盏"));
    assert(!agent_candidate_preview_local_first(&g,"请记住，我给这盏\xe7\x81"));
    assert(agent_candidate_preview_local_first(&g,"请记住，我给这盏灯"));
    assert(atomic_load(&g.request)==(AGENT_GUESS_THINK|AGENT_CANDIDATE_MEMORY));
    assert(!strcmp(source,"记下：我给这盏灯"));
    char frozen[65];strcpy(frozen,source);
    const char *reply="嗯，给这盏灯取名，我想想。";
    assert(!agent_candidate_reply(&g,reply)); /* Unheard verb is speculative. */
    const char *final="请记住，我给这盏灯取名叫小星星。";
    assert(agent_candidate_reply_final(&g,final,reply));
    assert(!agent_candidate_reply_final(&g,"请记住，我给这盏灯取下来收好。",reply));
    assert(!agent_candidate_reply_final(&g,"我给这盏灯取名叫小星星。",reply));
    assert(!agent_candidate_reply_final(&g,"请记住，我给另一盏灯取名叫小星星。",reply));
    assert(!agent_candidate_final(&g,final,false));
    agent_candidate_revision(&g,2);
    assert(!agent_candidate_preview_local_first(&g,final) && !g.revoked);
    assert(!strcmp(source,frozen) && agent_candidate_reply_final(&g,final,reply));
    assert(!agent_candidate_reply_final(&g,"请记住，我给这盏灯取名，取消。",reply));
    assert(!agent_candidate_reply_final(&g,"请记住，我给这盏灯取名，不对，改成",reply));
    assert(!agent_candidate_preview_local_first(&g,"取消。") && g.revoked);
    assert(!agent_candidate_reply_final(&g,final,reply));
    init(&g); /* Five chars do not loosen generic nomination. */
    assert(!agent_candidate_preview_local_first(&g,"我给这盏灯"));
    assert(agent_candidate_preview_local_first(&g,"我给这盏灯取"));
    assert(!strncmp(source,"待续：",9));
    init(&g); /* Same intent marker, separate language guard. */
    assert(!agent_candidate_preview_local_first(&g,"唔該記低，我鍾意藍"));
    assert(agent_candidate_preview_local_first(&g,"唔該記低，我鍾意藍色"));
    assert(atomic_load(&g.request)==(AGENT_GUESS_THINK|AGENT_CANDIDATE_MEMORY|AGENT_CANDIDATE_YUE));
    assert(!strcmp(source,"记下：我鍾意藍色"));
    assert(agent_candidate_reply_final(&g,"唔該記低，我鍾意藍色衫。","嗯，鍾意藍色，我諗諗先。"));
    assert(!agent_candidate_reply_final(&g,"唔該記低，我鍾意藍色衫。","嗯，鍾意藍色，我想想。"));
    puts("Early memory: object before verb, explicit intent, immutable source, no generic widening, wrong guesses/cancel/repair/language rejected");
}
static void final_topic_predictions(void)
{
    const char *partial="请记住，我给这盏灯取";
    const char *final="请记住，我给这盏灯取名叫小星星。";
    const char *prediction="嗯，灯取名，我想想。";
    agent_candidate_gate_t g;init(&g);
    assert(agent_candidate_preview_local_first(&g,partial));
    char original[65];strcpy(original,source);
    assert(!agent_candidate_reply(&g,prediction)); /* Original strict API. */
    assert(agent_candidate_reply_final(&g,final,prediction));
    assert(!agent_candidate_reply_final(&g,final,"嗯，灯的名字，我想想。")); /* Final has no 字. */
    assert(agent_candidate_reply_final(&g,"请记住，我给这盏灯取名字叫小星星。","嗯，灯的名字，我想想。"));
    assert(!strcmp(original,source) && !g.revoked);
    const char *wrong[]={"请记住，我给这盏灯取下来收好。","请记住，我给另一盏灯取名叫小星星。",
        "请记住，我给这盏灯取名叫小月亮。","请记住，我给这盏灯取名，取消。",
        "请记住，我给这盏灯取名，不对，改成", "请记住，我给这盏灯取名，用粤语回答。",
        "请记住。", "请介绍你自己。", "请记住，我给这盏灯取\xe5\x90"};
    for(unsigned i=0;i<sizeof(wrong)/sizeof(*wrong);++i)
        assert(!agent_candidate_reply_final(&g,wrong[i],i==2?"嗯，小星星，我想想。":prediction));
    const char *invented[]={"嗯，天气，我想想。","嗯，灯给取名，我想想。", /* Reordered. */
        "嗯，灯取名，我諗諗先。","已经记下了。","嗯，灯取名，我想想。完成了。",
        "嗯，灯取名，我想想\xe5\x90"};
    for(unsigned i=0;i<sizeof(invented)/sizeof(*invented);++i)
        assert(!agent_candidate_reply_final(&g,final,invented[i]));
    assert(!agent_candidate_reply_final(NULL,final,prediction));
    assert(!agent_candidate_reply_final(&g,NULL,prediction));
    assert(!agent_candidate_reply_final(&g,final,NULL));
    char large[AGENT_INPUT_MAX+2];memset(large,'a',sizeof(large)-1);large[sizeof(large)-1]=0;
    assert(!agent_candidate_reply_final(&g,large,prediction));
    agent_candidate_revision(&g,5);
    assert(!agent_candidate_preview_local_first(&g,final) && !g.revoked);
    assert(!strcmp(source,original) && agent_candidate_reply_final(&g,final,prediction));
    assert(!agent_candidate_preview_local_first(&g,"取消。") && g.revoked);
    assert(!agent_candidate_reply_final(&g,final,prediction));
    init(&g);assert(agent_candidate_preview_local_first(&g,"唔該記低，呢盞燈個名叫小"));
    const char *yue="唔該記低，呢盞燈個名叫小言。";
    const char *yue_reply="嗯，呢盞燈叫小言，我諗諗先。";
    assert(!agent_candidate_reply(&g,yue_reply));
    assert(agent_candidate_reply_final(&g,yue,yue_reply));
    assert(!agent_candidate_reply_final(&g,"唔該記低，呢盞燈個名叫小燕。",yue_reply));
    assert(!agent_candidate_reply_final(&g,yue,"嗯，呢盞燈叫小言，我想想。"));
    init(&g);assert(agent_candidate_preview_local_first(&g,"为什么天空是蓝色的？"));
    const char *answer="因为蓝光更容易被空气散射。";
    assert(agent_candidate_reply_final(&g,"为什么天空是蓝色的？",answer));
    assert(!agent_candidate_reply_final(&g,"为什么天空是蓝色的？那晚上呢？",answer));
}
int main(void)
{
    local_partials();complex_and_cancel();other_paths();memory_topics();early_memory_predictions();final_topic_predictions();
    puts("Local-first candidate: final-grounded predictions, immutable source, intent/language/cancellation; original strict API compatible");
}
