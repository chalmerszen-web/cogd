#ifndef COGD_P4_QUICK_INTENT_H
#define COGD_P4_QUICK_INTENT_H
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Whole utterances only: quoted phrases, negations and combined questions
 * must retain normal language handling. Never use a substring as a command. */
static inline bool p4_quick_plain(const char *text,char plain[101])
{
    if(!text || strlen(text)>100)return false;
    size_t n=0;
    for(const char *p=text;*p;){
        if(*p==' ' || *p=='?' || *p=='!' || *p=='.' || *p==','){++p;continue;}
        if(!strncmp(p,"？",3) || !strncmp(p,"！",3) || !strncmp(p,"。",3) || !strncmp(p,"，",3)){p+=3;continue;}
        plain[n++]=*p++;
    }
    plain[n]=0;return true;
}
static inline bool p4_quick_capture(const char *text)
{
    char plain[101];if(!p4_quick_plain(text,plain))return false;
    const char *p=plain;
    const char *const prefixes[]={"请","帮我","用摄像头"};
    for(size_t i=0;i<sizeof(prefixes)/sizeof(*prefixes);++i){
        size_t n=strlen(prefixes[i]);if(!strncmp(p,prefixes[i],n))p+=n;
    }
    static const char *const phrases[]={"拍题","重新拍题","重拍","重新拍照","再拍一张",
        "拍一下题目","拍一下这道题","拍一下当前题目","重新拍一下当前题目",
        "重新拍一下这道题","重新拍一下题目","重新拍当前题目","重拍当前题目",
        "拍一张题图","换一张题图","拍照读题"};
    for(size_t i=0;i<sizeof(phrases)/sizeof(*phrases);++i)if(!strcmp(p,phrases[i]))return true;
    return false;
}
static inline bool p4_quick_cancel(const char *text)
{
    char plain[101];if(!p4_quick_plain(text,plain))return false;
    const char *p=plain;
    if(!strncmp(p,"请",3))p+=3;
    if(!strncmp(p,"帮我",6))p+=6;
    static const char *const phrases[]={"取消拍题","取消这次拍题任务","取消当前拍题任务",
        "取消读题","停止读题","取消解题","取消当前解题任务"};
    for(size_t i=0;i<sizeof(phrases)/sizeof(*phrases);++i){
        size_t n=strlen(phrases[i]);if(strncmp(p,phrases[i],n))continue;
        const char *tail=p+n;
        if(!*tail || !strcmp(tail,"不用继续识别了") || !strcmp(tail,"不用继续了") ||
            !strcmp(tail,"不要继续识别了"))return true;
    }
    return false;
}
static inline bool p4_quick_confirm(const char *text)
{
    char plain[101];if(!p4_quick_plain(text,plain))return false;
    const char *const phrases[]={"是的","是的是的","对","对的","是这道题","就是这道题","对就是这题",
        "对就是这道题","是啊","是啊是啊","是啊是啊是啊你快点讲","是的开始解题","是的你讲吧",
        "开始解题","请开始解题","是这道题请开始解题","是的就是这道题请开始解题",
        "对就是这道题请开始解题","确认这道题请开始解题"};
    for(size_t i=0;i<sizeof(phrases)/sizeof(*phrases);++i)if(!strcmp(plain,phrases[i]))return true;
    return false;
}
typedef enum {P4_TUTOR_REQUEST_NONE,P4_TUTOR_REQUEST_FRESH,P4_TUTOR_REQUEST_CURRENT} p4_tutor_request_t;

/* Acquisition needs a positive request from the accepted user, not a model
 * tool choice. Consume a bounded whole utterance: merely mentioning a tutor
 * verb, a quotation, or a second unrelated clause grants no permission. */
static inline bool p4_tutor_take(const char **p,const char *word)
{
    size_t n=strlen(word);if(strncmp(*p,word,n))return false;
    *p+=n;return true;
}
static inline bool p4_tutor_request_plain(const char *text,char plain[385])
{
    if(!text || !*text || strlen(text)>384)return false;
    size_t n=0;
    for(const char *p=text;*p;){
        if(*p==' ' || *p=='\t' || *p=='\r' || *p=='\n' || *p=='?' || *p=='!' || *p=='.' || *p==','){++p;continue;}
        if(!strncmp(p,"？",3) || !strncmp(p,"！",3) || !strncmp(p,"。",3) || !strncmp(p,"，",3)){p+=3;continue;}
        plain[n++]=*p++;
    }
    plain[n]=0;return n!=0;
}
static inline const char *p4_tutor_request_prefix(const char *p,bool *camera)
{
    static const char *const words[]={"我想请你","能不能","可不可以","可以","好的","现在","麻烦","请你","请",
        "帮我","给我","替我","帮忙","你","那","嗯","呃","哦"};
    for(unsigned count=0;count<8;++count){
        if(p4_tutor_take(&p,"用摄像头") || p4_tutor_take(&p,"用镜头")){if(camera)*camera=true;continue;}
        bool consumed=false;
        for(size_t i=0;i<sizeof(words)/sizeof(*words);++i)
            if(p4_tutor_take(&p,words[i])){consumed=true;break;}
        if(!consumed)break;
    }
    return p;
}
static inline bool p4_tutor_request_end(const char *p)
{
    static const char *const words[]={"可以吗","好吗","行吗","吧","啊","呀","呢","呗","吗"};
    for(unsigned count=0;*p && count<3;++count){
        bool consumed=false;
        for(size_t i=0;i<sizeof(words)/sizeof(*words);++i)
            if(p4_tutor_take(&p,words[i])){consumed=true;break;}
        if(!consumed)return false;
    }
    return !*p;
}
/* A complete, explicit creative-story request needs no tutor operation.
 * Keep quoted/negative/combined requests on normal language handling. This
 * scopes tools for one accepted turn; it never clears the retained lesson. */
static inline bool p4_quick_story(const char *text)
{
    char plain[385];if(!p4_tutor_request_plain(text,plain))return false;
    bool camera=false;const char *p=p4_tutor_request_prefix(plain,&camera);
    if(camera)return false;
    if(p4_tutor_take(&p,"能")){p4_tutor_take(&p,"给我");}
    if(!(p4_tutor_take(&p,"我想要听") || p4_tutor_take(&p,"我想听") ||
         p4_tutor_take(&p,"讲述") || p4_tutor_take(&p,"创作") ||
         p4_tutor_take(&p,"讲") || p4_tutor_take(&p,"说") ||
         p4_tutor_take(&p,"编") || p4_tutor_take(&p,"写")))return false;
    p4_tutor_take(&p,"给我");
    const char *const articles[]={"一个","一篇","一则","一段","个","篇","则","段"};
    for(size_t i=0;i<sizeof(articles)/sizeof(*articles);++i)if(p4_tutor_take(&p,articles[i]))break;
    p4_tutor_take(&p,"大约");p4_tutor_take(&p,"约");
    unsigned digits=0;
    for(;digits<12;++digits){
        if(*p>='0' && *p<='9'){++p;continue;}
        const char *const numbers[]={"零","一","二","两","三","四","五","六","七","八","九","十","百","千","万"};
        bool consumed=false;
        for(size_t i=0;i<sizeof(numbers)/sizeof(*numbers);++i)if(p4_tutor_take(&p,numbers[i])){consumed=true;break;}
        if(!consumed)break;
    }
    if(digits){if(!p4_tutor_take(&p,"字"))return false;p4_tutor_take(&p,"左右");p4_tutor_take(&p,"的");}
    if(p4_tutor_take(&p,"关于")){
        const char *end=strstr(p,"的故事");
        if(!end || end==p || (size_t)(end-p)>192 || strstr(p,"这道题") || strstr(p,"照片") ||
           strstr(p,"镜头") || strstr(p,"屏幕") || strstr(p,"然后") || strstr(p,"之后") ||
           strstr(p,"之前") || strstr(p,"接着"))return false;
        return p4_tutor_request_end(end+strlen("的故事"));
    }
    const char *const descriptions[]={"有趣的","完整的","简短的","温暖的","长一点的","童话","寓言","睡前","冒险","科幻"};
    for(unsigned n=0;n<2;++n){
        bool consumed=false;
        for(size_t i=0;i<sizeof(descriptions)/sizeof(*descriptions);++i)if(p4_tutor_take(&p,descriptions[i])){consumed=true;break;}
        if(!consumed)break;
    }
    if(!p4_tutor_take(&p,"故事"))return false;
    p4_tutor_take(&p,"给我听");
    return p4_tutor_request_end(p);
}
/* Returns the first byte after a visual problem target, not an arbitrary
 * occurrence of 题. The caller must still consume the entire remainder. */
static inline const char *p4_tutor_request_target(const char *p)
{
    static const char *const targets[]={"镜头前的这道题","镜头前这道题","镜头前的题目","镜头前的题",
        "纸上的这道题","纸上这道题","纸上的题目","纸上的题","当前这道题","当前题目",
        "我这道题","这道数学题","这个数学题","这道题目","这道题","这个题","这题","题目","题"};
    for(size_t i=0;i<sizeof(targets)/sizeof(*targets);++i){
        const char *q=p;if(p4_tutor_take(&q,targets[i]))return q;
    }
    if(!p4_tutor_take(&p,"第"))return NULL;
    static const char *const numbers[]={"十二","十一","十","九","八","七","六","五","四","三","二","一",
        "12","11","10","9","8","7","6","5","4","3","2","1"};
    for(size_t i=0;i<sizeof(numbers)/sizeof(*numbers);++i){
        const char *q=p;
        if(p4_tutor_take(&q,numbers[i]) && (p4_tutor_take(&q,"道题") || p4_tutor_take(&q,"题")))return q;
    }
    return NULL;
}
static inline const char *p4_tutor_request_action(const char *p)
{
    static const char *const actions[]={"讲解","分析","识别","看看","读读","讲讲","看","读","解","做","算","讲"};
    for(size_t i=0;i<sizeof(actions)/sizeof(*actions);++i){
        const char *q=p;
        if(p4_tutor_take(&q,actions[i])){
            if(!p4_tutor_take(&q,"一下"))p4_tutor_take(&q,"一遍");
            return q;
        }
    }
    return NULL;
}
static inline bool p4_tutor_current_request(const char *p)
{
    p4_tutor_take(&p,"开始");
    const char *q=p4_tutor_request_action(p),*end=q?p4_tutor_request_target(q):NULL;
    if(end && p4_tutor_request_end(end))return true;
    q=p4_tutor_request_target(p);if(!q)return false;
    if(!strncmp(p,"第",3) && p4_tutor_request_end(q))return true; /* A whole ordinal selection. */
    const char *how=q;
    if(p4_tutor_take(&how,"应该怎么") || p4_tutor_take(&how,"怎么") || p4_tutor_take(&how,"如何")){
        end=p4_tutor_request_action(how);if(end && p4_tutor_request_end(end))return true;
    }
    /* The target is explicit before a natural request for help. Unlike a
     * global '不' exclusion, this keeps '这道题我不会做，帮我看看'. */
    if(p4_tutor_take(&q,"我不会做") || p4_tutor_take(&q,"我不会解") || p4_tutor_take(&q,"我看不懂")){
        if(!*q)return true;
        q=p4_tutor_request_prefix(q,NULL);end=p4_tutor_request_action(q);
        return end && p4_tutor_request_end(end);
    }
    return false;
}
static inline bool p4_tutor_capture_tail(const char *p)
{
    if(p4_tutor_request_end(p))return true;
    if(!p4_tutor_take(&p,"然后"))p4_tutor_take(&p,"再");
    p=p4_tutor_request_prefix(p,NULL);
    if(!p4_tutor_take(&p,"只"))p4_tutor_take(&p,"先");
    const char *q=p4_tutor_request_action(p),*end=q?p4_tutor_request_target(q):NULL;
    return end && p4_tutor_request_end(end);
}
static inline bool p4_tutor_fresh_request(const char *p)
{
    static const char *const complete[]={"重新拍照读题","拍照读题","重新拍照","重新拍题","拍照","拍题","换一张题图","换张题图"};
    for(size_t i=0;i<sizeof(complete)/sizeof(*complete);++i){
        const char *q=p;
        if(p4_tutor_take(&q,complete[i]) && p4_tutor_capture_tail(q))return true;
    }
    const char *q=p;bool repeat=p4_tutor_take(&q,"重新拍") || p4_tutor_take(&q,"重拍") || p4_tutor_take(&q,"再拍");
    if(!repeat && !p4_tutor_take(&q,"拍"))return false;
    if(repeat && p4_tutor_capture_tail(q))return true;
    bool sheet=p4_tutor_take(&q,"一张") || p4_tutor_take(&q,"张");
    bool brief=!sheet && p4_tutor_take(&q,"一下");
    const char *end=q;
    if(!p4_tutor_take(&end,"题图") && !p4_tutor_take(&end,"照片") && !p4_tutor_take(&end,"照"))
        end=p4_tutor_request_target(q);
    if(end && p4_tutor_capture_tail(end))return true;
    return repeat && (sheet || brief) && p4_tutor_capture_tail(q);
}
static inline p4_tutor_request_t p4_voice_tutor_request(const char *text,bool retry_pending)
{
    char plain[385];if(!p4_tutor_request_plain(text,plain) || p4_quick_confirm(plain))return P4_TUTOR_REQUEST_NONE;
    bool camera=false;const char *p=p4_tutor_request_prefix(plain,&camera);
    const char *confirmation=p;
    if(p4_quick_confirm(p) || (p4_tutor_take(&confirmation,"开始解题") && p4_tutor_request_end(confirmation)))
        return P4_TUTOR_REQUEST_NONE;
    if(p4_tutor_fresh_request(p))return P4_TUTOR_REQUEST_FRESH;
    if(p4_tutor_current_request(p))return camera?P4_TUTOR_REQUEST_FRESH:P4_TUTOR_REQUEST_CURRENT;
    if(retry_pending){
        static const char *const ready[]={"我放好了","放好了","我放了放了放了","重新试一次","再试一次","你读题啊","开始读题"};
        for(size_t i=0;i<sizeof(ready)/sizeof(*ready);++i)
            if(!strcmp(p,ready[i]))return P4_TUTOR_REQUEST_CURRENT;
    }
    return P4_TUTOR_REQUEST_NONE;
}
static inline bool p4_voice_tutor_start_allowed(const char *text,const char *phase,bool has_photo)
{
    bool retry=phase && (!strcmp(phase,"failed") || !strcmp(phase,"needs_clearer_view"));
    p4_tutor_request_t request=p4_voice_tutor_request(text,retry);
    return request==P4_TUTOR_REQUEST_FRESH || (request==P4_TUTOR_REQUEST_CURRENT &&
        phase && strcmp(phase,"reading") && (!has_photo || retry));
}
static const char *const p4_progress_phrases[]={"好了没","好了吗","好了没有","现在好了没","现在好了吗",
        "做完了吗","完成了吗","弄好了吗","读好了吗","解好了吗","算好了吗","解完了吗","读完了吗","算完了吗",
        "题目读好了吗","题目解好了吗","解题好了没","读题好了没","进度怎么样","进度怎样",
        "现在进度怎么样","现在到哪一步了","还在处理吗","还在解题吗","还在读题吗"};
static inline bool p4_quick_progress(const char *text)
{
    char plain[101];if(!p4_quick_plain(text,plain))return false;
    for(size_t i=0;i<sizeof(p4_progress_phrases)/sizeof(*p4_progress_phrases);++i)
        if(!strcmp(plain,p4_progress_phrases[i]))return true;
    return false;
}
#endif
