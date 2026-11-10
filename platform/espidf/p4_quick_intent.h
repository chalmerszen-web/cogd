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
static const char *const p4_progress_phrases[]={"好了没","好了吗","好了没有","现在好了没","现在好了吗",
        "做完了吗","完成了吗","弄好了吗","读好了吗","解好了吗","算好了吗",
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
