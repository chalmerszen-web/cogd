#include "intent.h"
#include "json.h"
#include <stddef.h>
#include <string.h>

bool agent_speech_meaningful_partial(const char *text)
{
    if(!text)return false;
    unsigned han=0,latin=0;
    const unsigned char *p=(const unsigned char *)text;
    for(;*p;++p) {
        if((*p>='a' && *p<='z') || (*p>='A' && *p<='Z'))++latin;
        if(*p>=0xe4 && *p<=0xe9 && p[1] && p[2]) {
            unsigned cp=(*p&15u)<<12|(p[1]&63u)<<6|(p[2]&63u);
            if(cp>=0x4e00 && cp<=0x9fff)++han;
        }
    }
    return han>=2 || latin>=3;
}

bool agent_speech_has_words(const char *text,const char *words)
{
    for(;*words;words+=strlen(words)+1)if(strstr(text,words))return true;
    return false;
}
static size_t trim(const char *text,size_t n)
{
    static const char punctuation[]="，。！？；：、…";
    while(n) {
        if(strchr(" \t\r\n,.!?;:",text[n-1])) {--n;continue;}
        const char *p=punctuation;
        while(*p && (n<3 || memcmp(text+n-3,p,3)))p+=3;
        if(!*p)break;
        n-=3;
    }
    return n;
}

bool agent_speech_unfinished_repair(const char *text)
{
    static const char repairs[]="不对\0不對\0不是\0唔係\0唔系\0";
    static const char tails[]="不对\0不對\0不是\0唔係\0唔系\0不\0不要\0唔\0唔好\0改成\0改为\0改為\0换成\0換成\0改做\0";
    if(!text)return false;
    size_t n=trim(text,strlen(text));
    bool repair=false;
    for(const char *word=repairs;*word;word+=strlen(word)+1) {
        const char *p=text;
        while((p=strstr(p,word))) {
            size_t at=(size_t)(p-text);
            /* A clause boundary avoids treating "对不对？" as a correction.
             * Quotes are deliberately not stripped from the final tail. */
            if(!at || trim(text,at)<at)repair=true;
            p+=strlen(word);
        }
    }
    if(!repair)return false;
    for(const char *word=tails;*word;word+=strlen(word)+1) {
        size_t length=strlen(word);
        if(n>=length && !memcmp(text+n-length,word,length))return true;
    }
    return false;
}
static bool take(const char **text,const char *prefix)
{
    size_t n=strlen(prefix);if(strncmp(*text,prefix,n))return false;
    *text+=n;return true;
}
static bool exact(const char *text,const char *word)
{ size_t n=strlen(word);return trim(text,strlen(text))==n && !memcmp(text,word,n); }
/* NUL-delimited alternatives keep the vocabulary in Flash without a pointer
 * table, and share matching code across the small admission grammar. */
static unsigned alternatives(const char **text,const char *words,bool whole)
{
    for(unsigned index=1;*words;words+=strlen(words)+1,++index) {
        if(whole ? exact(*text,words):!strncmp(*text,words,strlen(words))) {
            *text+=strlen(words);return index;
        }
    }
    return false;
}
#define PREFIX(text,words) alternatives(text,words "\0",false)
#define WORD(text,words) alternatives(text,words "\0",true)
static const char *polite(const char *text)
{
    text+=strspn(text," \t\r\n");
    (void)PREFIX(&text,"请\0請\0唔該\0唔该");
    (void)PREFIX(&text,"，\0,");
    return text;
}
static const char *lamp(const char *text)
{
    const char *p=polite(text);
    if(!PREFIX(&p,"把\0将\0將"))return NULL;
    (void)PREFIX(&p,"四颗\0四個\0四个\0所有");
    if(!PREFIX(&p,"灯\0燈"))return NULL;
    (void)take(&p,"光");return p;
}
const char *agent_speech_remember_content(const char *text)
{
    if(!text)return NULL;
    const char *p=polite(text);
    return PREFIX(&p,"记住\0記住\0記低\0记低")?p:NULL;
}
bool agent_speech_pending_argument(const char *text)
{
    if(!text)return false;
    if(agent_speech_unfinished_repair(text))return true;
    const char *p=polite(text);
    if(WORD(&p,"把\0将\0將\0记住\0記住\0記低\0记低"))return true;
    p=lamp(text);
    return p && (!trim(p,strlen(p)) || WORD(&p,
        "调\0調\0设\0設\0设置\0設置\0设为\0設為\0设置为\0設置為\0调成\0調成\0改成\0改为\0改為\0换成\0換成\0改做"));
}
bool agent_speech_phrase_pause(const char *text)
{
    /* These queries commonly have a following constraint or explanation.
     * Keep the known short identity request and literal commands fast. An
     * accidental match can only spend the small per-turn listening allowance. */
    if(!text || agent_speech_guess(text,true)==AGENT_GUESS_SELF)return false;
    return AGENT_SPEECH_HAS(text,"为什么\0為什麼\0为何\0為何\0怎么\0怎麼\0如何\0怎样\0怎樣\0"
        "點解\0点解\0點樣\0点样\0解释\0解釋");
}
static const char colour_words[]="红色\0紅色\0绿色\0綠色\0蓝色\0藍色\0黄色\0黃色\0青色\0紫色\0白色\0";
static const char colour_verbs[]="设为\0設為\0设置为\0設置為\0调成\0調成\0改成\0改为\0改為\0换成\0換成\0改做\0";
static unsigned colour_name(const char **text)
{
    static const uint8_t masks[]={0,4,4,2,2,1,1,6,6,3,5,7};
    return masks[alternatives(text,colour_words,false)];
}
static unsigned colour(const char **text)
{
    if(!alternatives(text,colour_verbs,false))return 0;
    return colour_name(text);
}
static bool boundary(const char **text)
{
    const char *start=*text;
    *text+=strspn(*text," \t\r\n");
    (void)PREFIX(text,"，\0,\0。\0.\0；\0;");
    *text+=strspn(*text," \t\r\n");
    return *text!=start;
}
static unsigned light_mask(const char *text)
{
    /* Fully consumed final input only. Repairs replace an unexecuted colour;
     * conditions, multiple actions and unknown suffixes remain model input. */
    const char *p=text?lamp(text):NULL;
    if(!p)return 0;
    unsigned mask=colour(&p);if(!mask)return false;
    for(unsigned repairs=0;;) {
        const char *next=p;
        if(!boundary(&next) || !PREFIX(&next,"不对\0不對\0唔係\0唔系\0不是"))break;
        if(++repairs>3)return false;
        /* "不是改成绿色" negates green; only a separate repair clause may
         * replace blue. ASR without this boundary stays on the model route. */
        if(!boundary(&next))return false;
        if(PREFIX(&next,"不要\0唔好")) {
            if(colour_name(&next)!=mask || !boundary(&next))return false;
        }
        mask=colour(&next);if(!mask)return false;
        p=next;
    }
    (void)take(&p,"吧");
    bool comma=PREFIX(&p,"，\0,");
    if(comma || PREFIX(&p,"。\0.")) {
        /* Segmented ASR appends a newline after the final sentence. Whitespace
         * alone is not an extra instruction; still reject every actual suffix. */
        if((comma || p[strspn(p," \t\r\n")]) && !PREFIX(&p,"请简单回答\0請簡單回答"))return false;
    }
    (void)PREFIX(&p,"。\0.");
    if(p[strspn(p," \t\r\n")])return false;
    return mask;
}
bool agent_speech_light_literal(const char *text,uint8_t rgb[3])
{
    if(!rgb)return false;
    unsigned mask=light_mask(text);if(!mask)return false;
    for(unsigned i=0;i<3;++i)rgb[i]=(mask&(4u>>i))?255:0;
    return true;
}
static bool word_prefix(const char *text,const char *words)
{
    size_t n=strlen(text);
    for(;*words;words+=strlen(words)+1)
        if(n<=strlen(words) && !memcmp(text,words,n))return true;
    return false;
}
bool agent_speech_light_local_preview(const char *text)
{
    /* Eligibility for avoiding unused synthesis, never execution authority.
     * Unknown suffixes return to generic preparation rather than being erased. */
    if(!text || !lamp(text))return false;
    if(light_mask(text) || agent_speech_pending_argument(text))return true;
    const char *p=lamp(text);
    if(word_prefix(p,colour_verbs))return true;
    for(const char *v=colour_verbs;*v;v+=strlen(v)+1) {
        size_t n=strlen(v);
        if(!strncmp(p,v,n) && word_prefix(p+n,colour_words))return true;
    }
    const char *tail=p;
    if(colour(&tail) && boundary(&tail) && word_prefix(tail,"不对\0不對\0不是\0唔係\0唔系\0"))return true;
    return false;
}
static bool question(const char *text)
{ return strchr(text,'?') || strstr(text,"？"); }
static const char *brief(const char *text)
{
    (void)PREFIX(&text,"用一句话\0用一句話");
    (void)PREFIX(&text,"简单\0簡單");
    return text;
}
agent_speech_guess_t agent_speech_guess(const char *text,bool final)
{
    if(!text)return AGENT_GUESS_NONE;
    const char *p=polite(text);
    if(AGENT_SPEECH_HAS(p,"取消\0算了\0算啦\0不用\0唔使\0不要记\0不要記\0别记\0別記\0唔好記"))return AGENT_GUESS_NONE;
    if(PREFIX(&p,"记住\0記住\0記低\0记低"))
        return !final || (trim(p,strlen(p)) && !question(p))?AGENT_GUESS_REMEMBER:AGENT_GUESS_NONE;
    p=polite(text);
    /* An optional addressee belongs to the introduction request, not to the
     * separate "你是谁" question. Keep final objects and suffixes exact. */
    const char *intro=p;
    if(PREFIX(&intro,"你\0您"))intro=polite(intro);
    intro=brief(intro);
    if(PREFIX(&intro,"介绍\0介紹")) {
        (void)PREFIX(&intro,"一下\0下");
        if(!final || WORD(&intro,"你自己\0自己"))return AGENT_GUESS_SELF;
        return AGENT_GUESS_NONE;
    }
    p=brief(p);
    if(WORD(&p,"你是谁\0你係邊個\0你叫什么名字\0你叫咩名"))return AGENT_GUESS_SELF;
    /* Keep a corrected colour's last explicit clause, never a negated colour
     * or question. This nominates a parameter-free receipt, not RGB values. */
    p=lamp(text);if(!p)return AGENT_GUESS_NONE;
    if(!final)return AGENT_GUESS_LIGHT;
    if(question(p))return AGENT_GUESS_NONE;
    size_t n=trim(p,strlen(p));const char *clause=p;
    for(size_t i=0;i<n;++i) {
        if(p[i]==',' || p[i]=='.')clause=p+i+1;
        if(!strncmp(p+i,"，",3) || !strncmp(p+i,"。",3))clause=p+i+3;
    }
    if(AGENT_SPEECH_HAS(clause,"不\0别\0別\0唔"))return AGENT_GUESS_NONE;
    if(colour(&clause) && !trim(clause,strlen(clause)))return AGENT_GUESS_LIGHT;
    return AGENT_GUESS_NONE;
}
const char *agent_speech_thinking_topic(const char *text,size_t *bytes)
{
    if(bytes)*bytes=0;
    if(!text || !agent_utf8_valid(text,strlen(text)))return NULL;
    const char *p=text;
    if(!PREFIX(&p,"嗯嗯\0嗯\0噢\0哦") || !PREFIX(&p,"，\0,"))return NULL;
    const char *topic=p;
    unsigned characters=0;
    while(*p && *p!=',' && strncmp(p,"，",3)) {
        unsigned char c=(unsigned char)*p;
        if((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9'))++p;
        else if(c>=0xe4 && c<=0xe9)p+=3;
        else return NULL;
        ++characters;
        if(characters>8 || p-topic>24)return NULL;
    }
    size_t n=(size_t)(p-topic);
    if(characters<2 || !PREFIX(&p,"，\0,") ||
       !WORD(&p,"我想想\0让我想想\0我諗諗先\0等我諗諗先"))return NULL;
    if(bytes)*bytes=n;
    return topic;
}
bool agent_speech_guess_reply(agent_speech_guess_t kind,const char *text)
{
    if(!text || !*text)return false;
    if(kind==AGENT_GUESS_ANSWER) {
        size_t n=strlen(text);
        const char *stop=strpbrk(text,".!\r\n");
        const char *han_stop=strstr(text,"。");if(!han_stop)han_stop=strstr(text,"！");
        /* Shape only, not a truth checker. Exact full-input agreement is
         * required before speech; late text changes abort without replay. */
        return n<=96 && agent_utf8_valid(text,n) && (!stop || stop==text+n-1) &&
            (!han_stop || han_stop==text+n-3) && !agent_speech_thinking_topic(text,NULL) &&
            !AGENT_SPEECH_HAS(text,"我想想\0让我想\0我諗\0准备\0準備\0已经\0已經\0已为\0已為\0转写\0轉寫\0草稿") &&
            !strchr(text,'?') && !strstr(text,"？") &&
            (text[n-1]=='.' || text[n-1]=='!' ||
             (n>=3 && (!memcmp(text+n-3,"。",3) || !memcmp(text+n-3,"！",3))));
    }
    if(kind==AGENT_GUESS_THINK)return agent_speech_thinking_topic(text,NULL)!=NULL;
    if(kind==AGENT_GUESS_SELF)return strstr(text,"小言") && !strstr(text,"转写") && !strstr(text,"草稿");
    /* An uncommitted task can promise preparation only. Do not admit names,
     * values or arbitrary suffixes that may refer to a superseded request. */
    const char *p=text;
    (void)PREFIX(&p,"嗯嗯\0好的\0嗯\0噢\0哦\0好");
    (void)PREFIX(&p,"，\0,");
    if(!PREFIX(&p,"我来\0让我\0我嚟\0等我"))return false;
    if(kind==AGENT_GUESS_REMEMBER)
        return WORD(&p,"记一下\0记下来\0記低先\0记低先");
    if(kind==AGENT_GUESS_LIGHT)
        return WORD(&p,"调整灯光\0调整一下灯光\0调一下灯光\0調下燈光先");
    return false;
}

unsigned agent_speech_route(const char *text)
{
    /* History speech needs a real result. A lookup never grants permission
     * for memory writes or richer hardware commands. */
    if(AGENT_SPEECH_HAS(text,
        "记住\0記住\0记下\0記下\0記低\0记低\0保存\0忘掉\0删除\0刪除\0"
        "引脚\0引腳\0管脚\0管腳\0GPIO\0gpio\0Gpio\0PWM\0pwm\0"
        "屏幕\0螢幕\0显示\0顯示\0LCD\0lcd\0时钟\0時鐘\0"
        "音乐\0音樂\0旋律\0乐曲\0樂曲\0歌曲\0播放\0录音\0錄音\0"
        "麦克风\0麥克風\0扬声器\0揚聲器\0喇叭\0音量\0音色"))return AGENT_SPEECH_ROUTE_FULL;
    if(strstr(text,"灯") || strstr(text,"燈")) {
        /* Negated/revised/compound lamp requests need semantic planning.
         * A mere mention of blue must never authorize setting it to blue. */
        if(AGENT_SPEECH_HAS(text,"不要\0别\0別\0不对\0不對\0不是\0"
            "唔好\0唔係\0唔系\0唔使\0先\0再\0如果\0或者"))return AGENT_SPEECH_ROUTE_FULL;
    }
    if(AGENT_SPEECH_HAS(text,
        "刚才\0剛才\0刚刚\0剛剛\0之前\0上次\0上回\0以前\0记得\0記得\0"
        "记忆\0記憶\0历史\0歷史\0头先\0頭先\0啱啱"))return AGENT_SPEECH_ROUTE_HISTORY;
    /* “我刚给这盏灯取的名字” is historical; “你叫什么名字” is not. */
    return AGENT_SPEECH_HAS(text,"刚\0剛") &&
        AGENT_SPEECH_HAS(text,"名字\0取名\0名称\0名稱")?AGENT_SPEECH_ROUTE_HISTORY:0;
}
