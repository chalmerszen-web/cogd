#include "candidate.h"
#include "json.h"
#include <string.h>

static const char source_marker[]="待续：";
static const char answer_marker[]="快答：";
static const char memory_marker[]="记下：";
_Static_assert(sizeof(memory_marker)==sizeof(source_marker),"Memory marker width");
_Static_assert(sizeof(source_marker)==sizeof(answer_marker),"Candidate marker width");

void agent_candidate_gate_init(agent_candidate_gate_t *g,char *source)
{
    memset(g,0,sizeof(*g));atomic_init(&g->request,0);atomic_init(&g->cancelled,false);
    g->source=source;if(source)source[0]=0;
}
static void revoke(agent_candidate_gate_t *g)
{ g->revoked=true;atomic_store_explicit(&g->cancelled,true,memory_order_release); }
static bool receipt(unsigned request)
{
    unsigned kind=request&AGENT_CANDIDATE_KIND;
    return kind==AGENT_GUESS_LIGHT || kind==AGENT_GUESS_REMEMBER || kind==AGENT_GUESS_THINK ||
        kind==AGENT_GUESS_ANSWER;
}
static bool withdrawn(const char *text)
{ return AGENT_SPEECH_HAS(text,"取消\0算了\0算啦\0不用\0唔使\0不对\0不對\0唔係\0唔系"); }
static bool informative(const char *text,bool memory)
{
    /* Politeness/formatting is not a topic. Counting "请用一句话介" as six
     * informative Han characters consumed the one candidate before the verb
     * arrived, then revoked it on "介绍". Wait for content, not an arbitrary
     * timer; a recognized intent still bypasses this generic receipt gate. */
    static const char scaffolding[]="请\0請\0唔該\0唔该\0你\0您\0用一句话\0用一句話\0简单\0簡單\0";
    for(;;) {
        text+=strspn(text," \t\r\n");
        if(!strncmp(text,"，",3))text+=3;
        else if(*text==',')++text;
        const char *p=scaffolding;
        while(*p && strncmp(text,p,strlen(p)))p+=strlen(p)+1;
        if(!*p)break;
        text+=strlen(p);
    }
    unsigned han=0,latin=0;
    for(const unsigned char *p=(const unsigned char *)text;*p;++p) {
        if((*p>='a' && *p<='z') || (*p>='A' && *p<='Z'))++latin;
        if(*p>=0xe4 && *p<=0xe9)++han;
    }
    /* Memory intent is already recognized. Five Han can identify its object
     * before an unfinished verb (我给这盏灯); this permits silent preparation,
     * not a fact, action or playback. Generic topics keep their original gate. */
    return han>=(memory?5u:6u) || latin>=12;
}
static const char *skip_separators(const char *p)
{
    static const char punctuation[]="，。！？；：、…";
    for(;;) {
        if(*p && strchr(" \t\r\n,.!?;:",*p)) {++p;continue;}
        const char *q=punctuation;
        while(*q && strncmp(p,q,3))q+=3;
        if(!*q)return p;
        p+=3;
    }
}
static bool same_prefix(const char *source,const char *final)
{
    for(;;) {
        source=skip_separators(source);final=skip_separators(final);
        if(!*source)return true;
        if(*source++!=*final++)return false;
    }
}
/* Only trim the outside. Internal decimal points, negations and clauses are
 * significant for answers, unlike a parameter-free thinking receipt. */
static size_t question_length(const char **text)
{
    *text+=strspn(*text," \t\r\n");size_t n=strlen(*text);
    while(n) {
        if(strchr(" \t\r\n.?!",(*text)[n-1]))--n;
        else if(n>=3 && (!memcmp(*text+n-3,"。",3) || !memcmp(*text+n-3,"？",3) ||
                          !memcmp(*text+n-3,"！",3)))n-=3;
        else break;
    }
    return n;
}
static bool same_question(const char *source,const char *final)
{
    size_t a=question_length(&source),b=question_length(&final);
    return a && a==b && !memcmp(source,final,a);
}
/* A small routing heuristic, not a semantic-completeness classifier. A
 * recognized prefix waits for punctuation instead of locking in a receipt.
 * Final byte equality, successful capture and tool-free generation remain
 * mandatory; a guessed early question can never authorize device actions. */
static bool quick_question(const char *text,bool *complete)
{
    *complete=false;
    if(agent_speech_route(text) || AGENT_SPEECH_HAS(text,
        "灯\0燈\0设备\0設備\0现在\0現在\0今日\0今天\0明天\0天气\0天氣\0"
        "最新\0新闻\0新聞\0股价\0股價\0时间\0時間\0取消\0不对\0不對\0唔係\0唔系\0"
        "如果\0先\0然后\0然後\0再\0你\0我\0这\0這\0它\0佢"))return false;
    text+=strspn(text," \t\r\n");
    bool why=!strncmp(text,"为什么",9) || !strncmp(text,"為什麼",9) ||
        !strncmp(text,"为何",6) || !strncmp(text,"為何",6) ||
        !strncmp(text,"點解",6) || !strncmp(text,"点解",6);
    bool definition=!strncmp(text,"什么是",9) || !strncmp(text,"甚麼是",9) ||
        !strncmp(text,"乜嘢係",9) || !strncmp(text,"咩係",6);
    if(!why && !definition)return false;
    size_t n=strlen(text);while(n && strchr(" \t\r\n",text[n-1]))--n;
    bool end=n && (text[n-1]=='?' || (n>=3 && !memcmp(text+n-3,"？",3)));
    unsigned han=0;
    for(const unsigned char *p=(const unsigned char *)text;*p;++p)if(*p>=0xe4 && *p<=0xe9)++han;
    *complete=end && han>=(why?8u:5u);
    return true;
}
void agent_candidate_revision(agent_candidate_gate_t *g,unsigned revision)
{
    unsigned request=atomic_load(&g->request);
    /* These preparation sentences contain no colour or memory value. A new
     * ASR sentence alone cannot make them stale; final intent still must match. */
    if(request && revision!=g->bound_revision && !receipt(request))revoke(g);
    g->revision=revision;
}
bool agent_candidate_cantonese(const char *text)
{
    return text && AGENT_SPEECH_HAS(text,"係邊個\0叫咩名\0廣東話\0广东话\0粤语\0粵語\0唔\0嘅\0喺\0記低\0记低\0點解\0点解\0乜嘢\0咩係");
}
static bool preview(agent_candidate_gate_t *g,const char *text,bool local_first)
{
    if(!text || g->revoked || atomic_load(&g->cancelled))return false;
    size_t bytes=strlen(text);
    if(!agent_utf8_valid(text,bytes))return false;
    agent_speech_guess_t kind=agent_speech_guess(text,false);
    unsigned request=atomic_load(&g->request);
    bool defer=local_first && kind==AGENT_GUESS_LIGHT && agent_speech_light_local_preview(text);
    bool lamp_topic=local_first && (request&AGENT_CANDIDATE_KIND)==AGENT_GUESS_THINK && g->source &&
        agent_speech_guess(g->source+sizeof(source_marker)-1,false)==AGENT_GUESS_LIGHT;
    if(local_first && kind==AGENT_GUESS_LIGHT && (!request || lamp_topic))kind=AGENT_GUESS_NONE;
    /* A memory topic retains its original intent without replacing the
     * published source when later ASR adds the value or another sentence. */
    if((request&AGENT_CANDIDATE_MEMORY) && kind==AGENT_GUESS_REMEMBER)kind=AGENT_GUESS_NONE;
    if(request) {
        if((kind && kind!=(request&AGENT_CANDIDATE_KIND)) ||
           AGENT_SPEECH_HAS(text,"取消\0算了\0算啦\0不用\0唔使") ||
           ((request&AGENT_CANDIDATE_KIND)!=AGENT_GUESS_LIGHT &&
            AGENT_SPEECH_HAS(text,"不对\0不對\0唔係\0唔系")))revoke(g);
        return false;
    }
    if(defer || !g->revision)return false;
    bool memory_topic=local_first && kind==AGENT_GUESS_REMEMBER;
    const char *content=memory_topic?agent_speech_remember_content(text):text;
    if(memory_topic)kind=AGENT_GUESS_NONE;
    if(!kind) {
        if(!g->source || withdrawn(text))return false;
        if(!content)return false;
        if(memory_topic)content=skip_separators(content);
        bytes=strlen(content);
        bool complete=false,question=!memory_topic && quick_question(content,&complete);
        size_t marker=sizeof(source_marker)-1;
        size_t available=AGENT_CANDIDATE_SOURCE_BYTES-1-marker;
        bool answer=question && bytes<=available;
        if(answer && !complete)return false;
        if(!answer && !informative(content,memory_topic))return false;
        if(bytes>available) {
            bytes=available;
            while(((unsigned char)content[bytes]&0xc0)==0x80)--bytes;
        }
        memcpy(g->source,answer?answer_marker:memory_topic?memory_marker:source_marker,marker);
        memcpy(g->source+marker,content,bytes);g->source[marker+bytes]=0;
        if(!answer && !informative(g->source+marker,memory_topic))return false;
        kind=answer?AGENT_GUESS_ANSWER:AGENT_GUESS_THINK;
    }
    g->bound_revision=g->revision;
    request=(unsigned)kind|(agent_candidate_cantonese(text)?AGENT_CANDIDATE_YUE:0)|
        (memory_topic?AGENT_CANDIDATE_MEMORY:0);
    atomic_store_explicit(&g->request,request,memory_order_release);return true;
}
bool agent_candidate_preview(agent_candidate_gate_t *g,const char *text)
{ return preview(g,text,false); }
bool agent_candidate_preview_local_first(agent_candidate_gate_t *g,const char *text)
{ return preview(g,text,true); }
bool agent_candidate_final(const agent_candidate_gate_t *g,const char *text,bool capture_ok)
{
    unsigned request=atomic_load(&g->request);
    unsigned kind=request&AGENT_CANDIDATE_KIND;
    if(!capture_ok || !text || !request || g->revoked || atomic_load(&g->cancelled) ||
       (!receipt(request) && g->revision!=g->bound_revision) ||
       agent_speech_unfinished_repair(text))return false;
    uint8_t rgb[3];
    const char *content=(request&AGENT_CANDIDATE_MEMORY)?agent_speech_remember_content(text):text;
    /* This admits a parameter-free preparation sentence, never an action.
     * Require the WHOLE final lamp request, including any resolved repairs;
     * questions, cancellations, extra actions and missing prefixes fail. */
    bool matches=kind==AGENT_GUESS_LIGHT?agent_speech_light_literal(text,rgb):
        kind==AGENT_GUESS_ANSWER?(!withdrawn(text) && same_question(g->source+sizeof(answer_marker)-1,text)):
        kind==AGENT_GUESS_THINK?(content && !withdrawn(text) &&
            (!(request&AGENT_CANDIDATE_MEMORY) || agent_speech_guess(text,true)==AGENT_GUESS_REMEMBER) &&
            same_prefix(g->source+sizeof(source_marker)-1,content)):
        agent_speech_guess(text,true)==kind;
    return matches &&
        agent_candidate_cantonese(text)==((request&AGENT_CANDIDATE_YUE)!=0);
}
const char *agent_candidate_text(const agent_candidate_gate_t *g)
{
    unsigned request=atomic_load_explicit(&g->request,memory_order_acquire);
    unsigned kind=request&AGENT_CANDIDATE_KIND;
    return kind==AGENT_GUESS_THINK || kind==AGENT_GUESS_ANSWER?g->source:agent_candidate_prompt(request);
}
static bool topic_matches(unsigned request,const char *source,const char *text)
{
    size_t bytes;const char *topic=agent_speech_thinking_topic(text,&bytes);
    if(!topic || !source)return false;
    bool yue=AGENT_SPEECH_HAS(text,"我諗諗先\0等我諗諗先");
    if(yue!=((request&AGENT_CANDIDATE_YUE)!=0))return false;
    /* Topic content stays an ordered source subsequence. A single internal
     * possessive particle is grammatical glue (灯的颜色), not a new state.
     * It cannot replace either side of the topic or introduce a factual word.
     * This admits a thinking receipt only, not a factual or action claim. */
    const char *p=source;
    unsigned matched=0;bool connector=false;
    while(bytes) {
        size_t width=(unsigned char)*topic<0x80?1:3; /* Shape check permits ASCII/Han. */
        if(!connector && matched && bytes>3 && !strncmp(topic,yue?"嘅":"的",3)) {
            connector=true;topic+=3;bytes-=3;continue;
        }
        while(*p && (((unsigned char)*p&0xc0)==0x80 || strncmp(p,topic,width)))++p;
        if(!*p)return false;
        p+=width;topic+=width;bytes-=width;++matched;
    }
    return matched>=2;
}
bool agent_candidate_reply(const agent_candidate_gate_t *g,const char *text)
{
    unsigned request=atomic_load_explicit(&g->request,memory_order_acquire);
    unsigned kind=request&AGENT_CANDIDATE_KIND;
    if(kind!=AGENT_GUESS_THINK)return agent_speech_guess_reply(kind,text);
    return topic_matches(request,g->source+sizeof(source_marker)-1,text);
}
bool agent_candidate_reply_final(const agent_candidate_gate_t *g,const char *final,const char *text)
{
    if(!g || !final)return false;
    size_t bytes=strlen(final);
    if(bytes>AGENT_INPUT_MAX || !agent_utf8_valid(final,bytes))return false;
    unsigned request=atomic_load_explicit(&g->request,memory_order_acquire);
    unsigned kind=request&AGENT_CANDIDATE_KIND;
    if((kind==AGENT_GUESS_THINK || kind==AGENT_GUESS_ANSWER) && !g->source)return false;
    /* The speculative topic may finish an unheard word, but it never grants
     * speech authority. Full capture must retain the original nomination;
     * all topic content must then occur in the actual final input. */
    if(!agent_candidate_final(g,final,true))return false;
    if(kind!=AGENT_GUESS_THINK)return agent_candidate_reply(g,text);
    const char *source=(request&AGENT_CANDIDATE_MEMORY)?agent_speech_remember_content(final):final;
    return topic_matches(request,source,text);
}
const char *agent_candidate_prompt(unsigned request)
{
    bool yue=(request&AGENT_CANDIDATE_YUE)!=0;
    switch(request&AGENT_CANDIDATE_KIND) {
    case AGENT_GUESS_SELF:return yue?"你係邊個？用一句廣東話介紹自己。":"请用一句话介绍你自己。";
    case AGENT_GUESS_LIGHT:return yue?"請準備調整燈光，用廣東話表示準備，顏色稍後確定。":"请准备调整灯光，具体颜色稍后确定。";
    case AGENT_GUESS_REMEMBER:return yue?"請用廣東話表示準備記低內容，具體內容稍後確定。":"请表示准备记下内容，具体内容稍后确定。";
    default:return NULL;
    }
}
