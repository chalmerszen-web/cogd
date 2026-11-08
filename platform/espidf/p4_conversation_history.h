#ifndef COGD_P4_CONVERSATION_HISTORY_H
#define COGD_P4_CONVERSATION_HISTORY_H
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* An assistant can mistakenly repeat an application label. Keep the user's
 * utterance in the journal, but do not teach the next session to repeat that
 * invalid answer. A user asking about a label remains ordinary user content. */
static inline bool p4_history_placeholder(const char *text)
{
    if(!text)return false;
    text+=strspn(text," \t\r\n");
    /* Observed cloud placeholder, not a completed answer. Match the whole
     * trimmed value so explanations and user quotations remain valid. */
    static const char *const placeholders[]={"[生成中]","[工具调用]","[生成失败]","[history]"};
    for(unsigned i=0;i<sizeof(placeholders)/sizeof(placeholders[0]);++i) {
        size_t length=strlen(placeholders[i]);
        if(!strncmp(text,placeholders[i],length) && !text[length+strspn(text+length," \t\r\n")])return true;
    }
    /* Observed native reply: [tool: conversation_jobs], without any structured
     * function call. Match a whole marker, never prose explaining/quoting it. */
    if(!strncmp(text,"[tool:",6)) {
        const char *name=text+6;name+=strspn(name," \t");
        size_t length=strspn(name,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
        const char *end=name+length;end+=strspn(end," \t");
        if(length && *end==']' && !end[1+strspn(end+1," \t\r\n")])return true;
    }
    return false;
}
static inline bool p4_history_internal_answer(const char *text)
{
    if(!text)return false;
    if(p4_history_placeholder(text))return true;
    text+=strspn(text," \t\r\n");
    static const char *const prefixes[]={
        "[设备通知","【设备通知","【设备自动通知",
        "[上一条设备回复含内部通知","[用户已打断当前对话]"
    };
    for(unsigned i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);++i)
        if(!strncmp(text,prefixes[i],strlen(prefixes[i])))return true;
    return false;
}
static inline bool p4_history_keep_answer(const char *role,const char *text)
{
    return !role || strcmp(role,"assistant") || !p4_history_internal_answer(text);
}

/* The journal records an interrupted reply as metadata, never as speech.
 * Do not classify user quotations. */
static inline bool p4_history_interrupted_answer(const char *role,const char *text)
{
    if(!role || strcmp(role,"assistant") || !text)return false;
    text+=strspn(text," \t\r\n");
    static const char *const prefixes[]={
        "[上一条回复被打断", "[Reply interrupted", "[用户已打断当前对话]"
    };
    for(unsigned i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);++i)
        if(!strncmp(text,prefixes[i],strlen(prefixes[i])))return true;
    return false;
}

/* Preserve the preceding user's facts and chronological priority. Dropping
 * both live items lost a newly supplied name after an interruption; dropping
 * only the answer made the old request look unanswered. A balanced historical
 * pair closes that request without restoring internal instructions as speech.
 * The current ASR item is appended after all historical acknowledgements. */
static inline const char *p4_history_restored_content(const char *role,const char *text)
{
    if(p4_history_interrupted_answer(role,text))return "这条回复的播放已中断。";
    if(!p4_history_keep_answer(role,text))return "这条回复未能完成。";
    return text;
}

/* Historical items must all be acknowledged before live Audio is admitted.
 * A stale/duplicate item-created event cannot open the next connection early. */
enum { P4_HISTORY_RESTORE_MAX=8 };
typedef struct {unsigned generation,count,pending;} p4_history_restore_t;
static inline bool p4_history_restore_begin(p4_history_restore_t *s,unsigned generation,unsigned count)
{
    if(count>P4_HISTORY_RESTORE_MAX)return false;
    *s=(p4_history_restore_t){generation,count,(1u<<count)-1u};return true;
}
static inline bool p4_history_restore_id(const p4_history_restore_t *s,unsigned index,char *out,size_t cap)
{
    if(index>=s->count || !out || !cap)return false;
    int n=snprintf(out,cap,"cogd_history_%u_%u",s->generation,index);
    return n>0 && (size_t)n<cap;
}
static inline int p4_history_restore_index(const p4_history_restore_t *s,const char *id)
{
    if(!id)return -1;
    for(unsigned i=0;i<s->count;++i){
        char expected[64];
        if(p4_history_restore_id(s,i,expected,sizeof(expected)) && !strcmp(expected,id))return (int)i;
    }
    return -1;
}
static inline bool p4_history_restore_ack(p4_history_restore_t *s,const char *id)
{
    int i=p4_history_restore_index(s,id);if(i<0)return false;
    s->pending&=~(1u<<(unsigned)i);return true;
}
#endif
