#ifndef P4_STATUS_HINT_H
#define P4_STATUS_HINT_H
#include "p4_quick_intent.h"
/* A provider can emit stash="好了没" then an empty final for that same item.
 * This is ONLY a read-only status intent hint, never a confirmed transcript,
 * user history item, or permission to execute any other command. Keep only
 * the latest whole-utterance candidate; revisions and identity changes win. */
typedef struct {char item[129],text[101];bool blocked;} p4_status_hint_t;
static inline bool p4_status_hint_excluded(const char *text)
{
    if(strstr(text,"不") || strstr(text,"别") || strstr(text,"取消") ||
       strpbrk(text,"\"'") || strstr(text,"“") || strstr(text,"”") ||
       strstr(text,"‘") || strstr(text,"’"))return true;
    if(p4_quick_progress(text))return false;
    for(size_t i=0;i<sizeof(p4_progress_phrases)/sizeof(*p4_progress_phrases);++i)
        if(strstr(text,p4_progress_phrases[i]))return true;
    return false;
}
static inline void p4_status_hint_update(p4_status_hint_t *s,const char *item,
                                        const char *text,const char *stash)
{
    if(!item || !*item || strlen(item)>=sizeof(s->item) || !text || !stash){s->blocked=true;return;}
    if(!s->item[0])strcpy(s->item,item);
    else if(strcmp(s->item,item)){s->blocked=true;return;}
    if(!*text && !*stash)return;
    s->text[0]=0;
    const char *candidate=!*text?stash:!*stash?text:NULL;
    /* Stash is explicitly provisional: real capture revised "he喽" into
     * "好了没". Do not freeze that short unstable error. Stable text,
     * explicit exclusions and a complete phrase with extra clauses do veto
     * fallback for this utterance. Nothing here promotes stash into ASR. */
    if(!candidate || (*text && !p4_quick_progress(text)) ||
       (candidate && p4_status_hint_excluded(candidate))){s->blocked=true;return;}
    if(!s->blocked && p4_quick_progress(candidate))strcpy(s->text,candidate);
}
static inline bool p4_status_hint_matches(const p4_status_hint_t *s,const char *item)
{ return item && *item && !s->blocked && s->text[0] && !strcmp(s->item,item); }
#endif
