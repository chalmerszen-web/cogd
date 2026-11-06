#ifndef P4_RESPONSE_OWNER_H
#define P4_RESPONSE_OWNER_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* One response per user turn or delivered application item. An automatic
 * continuation and our manual fallback spend the same ticket, including when
 * response.created arrives before conversation.item.created. Actor-owned. */
typedef struct {
    unsigned ticket;
    bool pending, silent, acknowledged, manual, tools_authorized, user_response;
    uint64_t acknowledged_at;
} p4_response_owner_t;

static inline unsigned p4_response_begin(p4_response_owner_t *s, bool silent)
{
    ++s->ticket;
    s->pending=true;s->silent=silent;s->acknowledged=false;s->manual=false;s->user_response=false;
    return s->ticket;
}
/* Application notices carry results, never fresh authorization for side
 * effects. Neither a receipt nor an automatic continuation restores it. */
static inline unsigned p4_response_notice(p4_response_owner_t *s)
{ s->tools_authorized=false;return p4_response_begin(s,false); }
static inline unsigned p4_response_user(p4_response_owner_t *s)
{
    s->tools_authorized=true;
    unsigned ticket=p4_response_begin(s,false);s->user_response=true;return ticket;
}
static inline bool p4_response_tools(const p4_response_owner_t *s)
{ return s->tools_authorized; }
/* The conversational schema and execution gate share this restriction.
 * Direct USB/cloud fixture tools do not call this voice-only policy. */
static inline bool p4_voice_tool_field_allowed(const char *tool,const char *field)
{
    return !tool || !field || strcmp(tool,"device_tutor") || strcmp(field,"test_image_url");
}
static inline void p4_response_obsolete(p4_response_owner_t *s)
{
    ++s->ticket;s->pending=false;s->acknowledged=false;s->manual=false;
}
static inline bool p4_response_audible(const p4_response_owner_t *s)
{ return s->pending && !s->silent; }
static inline bool p4_response_created(p4_response_owner_t *s)
{
    bool audible=p4_response_audible(s);
    s->pending=false;s->acknowledged=false;s->manual=false;
    return audible;
}
static inline void p4_response_ack(p4_response_owner_t *s, unsigned ticket, uint64_t now)
{
    if(ticket==s->ticket && s->pending && !s->acknowledged){s->acknowledged=true;s->acknowledged_at=now;}
}
static inline bool p4_response_manual_due(const p4_response_owner_t *s, uint64_t now)
{
    return p4_response_audible(s) && s->acknowledged && !s->manual &&
        now-s->acknowledged_at>=250;
}
static inline bool p4_response_user_pending(const p4_response_owner_t *s,unsigned ticket)
{ return s->user_response && s->pending && s->ticket==ticket; }
static inline void p4_response_user_ready(p4_response_owner_t *s,unsigned ticket,
    bool transcribed,bool speech_stopped,bool session_ready,uint64_t now)
{
    if(p4_response_user_pending(s,ticket) && transcribed && speech_stopped && session_ready)
        p4_response_ack(s,ticket,now);
}
static inline bool p4_response_user_due(const p4_response_owner_t *s,unsigned ticket,uint64_t now)
{
    /* Give VAD's automatic response a longer grace than a tool continuation. */
    return p4_response_user_pending(s,ticket) && p4_response_manual_due(s,now) && now-s->acknowledged_at>=1000;
}
static inline bool p4_response_silent_wait(const p4_response_owner_t *s, uint64_t now)
{
    return s->pending && s->silent && (!s->acknowledged || now-s->acknowledged_at<250);
}
static inline unsigned p4_response_manual(p4_response_owner_t *s)
{ s->manual=true;return s->ticket; }
static inline bool p4_response_send(const p4_response_owner_t *s, unsigned ticket)
{ return ticket==s->ticket && s->manual && p4_response_audible(s); }

/* A labelled app message is temporary provider context, not a human turn.
 * Keep its identity past creation ACK; remove only this item after its reply
 * finishes or a real user supersedes it. Assistant repair owns separate IDs. */
typedef struct {
    char item[64],response[129];
    unsigned ticket;
    bool created,retired,delete_sent;
    uint64_t delete_at;
} p4_notice_item_t;
static inline bool p4_notice_item_begin(p4_notice_item_t *s,const char *id,unsigned ticket)
{
    if(s->item[0] || !id || !*id || strlen(id)>=sizeof(s->item))return false;
    memset(s,0,sizeof(*s));strcpy(s->item,id);s->ticket=ticket;return true;
}
static inline void p4_notice_item_created(p4_notice_item_t *s,const char *id)
{ if(s->item[0] && id && !strcmp(s->item,id))s->created=true; }
static inline void p4_notice_item_response(p4_notice_item_t *s,unsigned ticket,const char *id)
{
    if(s->item[0] && !s->retired && s->ticket==ticket && id && *id && strlen(id)<sizeof(s->response))
        strcpy(s->response,id);
}
static inline void p4_notice_item_retire(p4_notice_item_t *s)
{ if(s->item[0])s->retired=true; }
static inline void p4_notice_item_done(p4_notice_item_t *s,const char *id)
{ if(s->response[0] && id && !strcmp(s->response,id))p4_notice_item_retire(s); }
static inline bool p4_notice_item_deleting(const p4_notice_item_t *s)
{ return s->item[0] && s->retired; }
static inline bool p4_notice_item_delete_due(const p4_notice_item_t *s,bool active)
{ return p4_notice_item_deleting(s) && s->created && !s->delete_sent && !active; }
static inline void p4_notice_item_delete_sent(p4_notice_item_t *s,uint64_t now)
{ s->delete_sent=true;s->delete_at=now; }
static inline bool p4_notice_item_deleted(p4_notice_item_t *s,const char *id)
{
    if(!s->delete_sent || !id || strcmp(s->item,id))return false;
    memset(s,0,sizeof(*s));return true;
}

/* Claim before enqueue/playback, not after successful playback. Interruption
 * must not turn the same completed side effect into a fresh announcement. */
static inline bool p4_notice_claim(bool *consumed, bool current)
{
    if(*consumed)return false;
    *consumed=true;return current;
}
/* A recorded study preference is not solution content. Only the exact tutor
 * operation/identity and running phase may suppress this receipt's speech. */
static inline bool p4_tutor_pending_choice(const char *tool,const char *action,
    const char *state,const char *phase,const char *pending,bool identity_matches)
{
    if(!tool || !action || !state || !phase || !pending || !identity_matches ||
       strcmp(tool,"device_tutor") || strcmp(state,"running") || strcmp(phase,"solving"))return false;
    return (!strcmp(action,"read") && !strcmp(pending,"reading")) ||
        (!strcmp(action,"explain") && !strcmp(pending,"explaining"));
}
typedef enum { P4_TUTOR_STAGE_NONE, P4_TUTOR_STAGE_CONFIRM, P4_TUTOR_STAGE_SELECT } p4_tutor_stage_t;
static inline p4_tutor_stage_t p4_tutor_stage(const char *action,const char *phase,
    bool succeeded,bool same_user,bool same_snapshot)
{
    if(!action || !phase || !succeeded || !same_user || !same_snapshot ||
       (strcmp(action,"start") && strcmp(action,"refine")))return P4_TUTOR_STAGE_NONE;
    if(!strcmp(phase,"awaiting_confirmation"))return P4_TUTOR_STAGE_CONFIRM;
    if(!strcmp(phase,"needs_selection"))return P4_TUTOR_STAGE_SELECT;
    return P4_TUTOR_STAGE_NONE;
}
/* Deliberately accepts only the current summary/candidates, never a whole
 * result with overview/excerpt or historical instructions. This is a scoped
 * model prompt, not a claim that realtime audio is deterministic TTS. */
static inline bool p4_tutor_stage_message(p4_tutor_stage_t stage,const char *summary,char *out,size_t cap)
{
    if(!out || !cap)return false;
    out[0]=0;
    if(!summary || !*summary || strlen(summary)>1400 ||
       (stage!=P4_TUTOR_STAGE_CONFIRM && stage!=P4_TUTOR_STAGE_SELECT))return false;
    const char *prefix="【设备当前阶段通知，不是用户发言】本次只朗读下面短文本一次，然后停止。不得解释题目、推导或给出答案，不引用历史，不问先讲还是先看，不调用工具。仅朗读：\n";
    const char *question=stage==P4_TUTOR_STAGE_CONFIRM?"\n是这道题吗？":"\n你要选哪一道题？请告诉我题号。";
    if(strlen(prefix)+strlen(summary)+strlen(question)+1>cap)return false;
    snprintf(out,cap,"%s%s%s",prefix,summary,question);return true;
}
typedef enum { P4_TUTOR_NOTICE_SKIP, P4_TUTOR_NOTICE_RESULT, P4_TUTOR_NOTICE_COMPLETION, P4_TUTOR_NOTICE_FAILURE } p4_tutor_notice_t;
/* A slow synchronous action still owes its caller a result after an accepted
 * receipt. Only the solve/recheck job consumes the lesson completion claim. */
static inline p4_tutor_notice_t p4_tutor_notice_kind(const char *action,bool same_user,
    bool same_snapshot,const char *phase,bool failed,bool completion_pending,bool done)
{
    if(!action || !*action || !phase || !same_snapshot || !strcmp(phase,"cancelled") || !strcmp(phase,"superseded"))return P4_TUTOR_NOTICE_SKIP;
    if(!strcmp(action,"confirm") || !strcmp(action,"recheck")) {
        /* Choosing how to study while solving does not revoke failure feedback
         * for this question. A failed recheck can retain a complete old lesson. */
        if(!strcmp(phase,"failed") || !strcmp(phase,"incomplete") || (failed && !strcmp(phase,"complete")))return P4_TUTOR_NOTICE_FAILURE;
        if(!strcmp(phase,"complete"))return completion_pending && !done?P4_TUTOR_NOTICE_COMPLETION:P4_TUTOR_NOTICE_SKIP;
    }
    return same_user?P4_TUTOR_NOTICE_RESULT:P4_TUTOR_NOTICE_SKIP;
}
#endif
