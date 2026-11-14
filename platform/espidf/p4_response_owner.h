#ifndef P4_RESPONSE_OWNER_H
#define P4_RESPONSE_OWNER_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A scoped answer is ready-to-speak data, not a schema for the model to
 * interpret. In a recorded reply, numeric step_count=3 became the spoken
 * placeholder [step_count]. Keep action/identity fields out of this payload. */
static inline int p4_tutor_scope_message(const char *action,bool succeeded,unsigned count,
    const char *answer,char *out,size_t cap)
{
    if(!action || (strcmp(action,"step_count") && strcmp(action,"answer_only")))return 0;
    if(!out || !cap)return -1;
    const char *prefix="本轮用户仅要简短结果。以下是设备从当前已确认解答取得的回复正文。只说回复正文，不添加解释、标记或字段名，不调用工具：\n";
    int n;
    if(!strcmp(action,"step_count")) {
        if(succeeded && count && count<=128)n=snprintf(out,cap,"%s共有%u个步骤。",prefix,count);
        else n=snprintf(out,cap,"%s目前还不能确认步骤数量。",prefix);
    } else n=snprintf(out,cap,"%s%s",prefix,succeeded && answer && *answer?answer:"目前还不能确认最终答案。");
    return n>=0 && (size_t)n<cap?1:-1;
}

/* Bound consecutive recovery failures, not the lifetime of a conversation.
 * A completely played answer to a real user opens a fresh allowance. Merely
 * connecting or playing the automatic recovery notice does not. */
typedef struct {unsigned attempts;} p4_network_recovery_t;
static inline void p4_network_recovery_progress(p4_network_recovery_t *s,bool completed_user_turn)
{ if(completed_user_turn)s->attempts=0; }
static inline bool p4_network_recovery_retry(p4_network_recovery_t *s)
{
    if(s->attempts)return false;
    ++s->attempts;return true;
}

/* Keep an undelivered recovery notice across empty-input retries. A speech
 * candidate pauses it; only accepted input or completed playback consumes it. */
typedef struct {uint64_t due;bool pending,queued;} p4_recovery_notice_t;
static inline void p4_recovery_notice_begin(p4_recovery_notice_t *s,uint64_t now)
{ s->due=now+1200;s->pending=true;s->queued=false; }
static inline void p4_recovery_notice_finish(p4_recovery_notice_t *s)
{ s->pending=false;s->queued=false; }
static inline bool p4_recovery_notice_due(p4_recovery_notice_t *s,uint64_t now,bool user_accepted,bool quiet)
{
    if(!s->pending)return false;
    if(user_accepted){p4_recovery_notice_finish(s);return false;}
    if(s->queued || !quiet || now<s->due)return false;
    s->queued=true;return true;
}

/* A queued application result is still unheard. A rejected microphone
 * candidate may defer it, but must not consume it or repeat its side effect. */
typedef struct {unsigned ticket;bool pending,queued;} p4_notice_delivery_t;
static inline void p4_notice_delivery_begin(p4_notice_delivery_t *s)
{ *s=(p4_notice_delivery_t){.pending=true}; }
static inline void p4_notice_delivery_queued(p4_notice_delivery_t *s,unsigned ticket)
{ s->ticket=ticket;s->queued=true; }
static inline bool p4_notice_delivery_defer(p4_notice_delivery_t *s)
{
    if(!s->pending || !s->queued)return false;
    s->queued=false;return true;
}
static inline bool p4_notice_delivery_heard(p4_notice_delivery_t *s,unsigned ticket)
{
    if(!s->pending || !s->queued || s->ticket!=ticket)return false;
    *s=(p4_notice_delivery_t){0};return true;
}
typedef struct {
    unsigned job,question,revision;
    char phase[40],teaching[24];
} p4_notice_snapshot_t;
static inline bool p4_notice_snapshot_same(const p4_notice_snapshot_t *a,const p4_notice_snapshot_t *b)
{
    return a->job && a->job==b->job && a->question==b->question && a->revision==b->revision &&
        !strcmp(a->phase,b->phase) && !strcmp(a->teaching,b->teaching);
}

/* A committed ASR item is only a candidate. Empty recognitions must not
 * replace the user who owns a pending tutor result. Keep that identity until
 * a nonempty, validated transcript actually accepts a new user request. */
typedef struct { char item[129]; } p4_input_identity_t;
static inline bool p4_input_identity_accept(p4_input_identity_t *s,const char *item,const char *text)
{
    if(!item || !*item || strlen(item)>=sizeof(s->item) || !text ||
       !text[strspn(text," \t\r\n")])return false;
    strcpy(s->item,item);return true;
}
static inline bool p4_input_identity_matches(const p4_input_identity_t *s,const char *item)
{ return item && *item && !strcmp(s->item,item); }

/* Bind a recognized Audio item to one appended text item before inference.
 * The provider can otherwise answer a restored text turn instead of the
 * latest committed audio. This is one human request, not a second turn. */
typedef struct {
    char item[64],source[129];
    unsigned ticket;
    uint64_t started;
    bool pending;
} p4_input_binding_t;
static inline bool p4_input_binding_begin(p4_input_binding_t *s,const char *item,
    const char *source,unsigned ticket,uint64_t now)
{
    if(s->pending || !ticket || !item || !*item || strlen(item)>=sizeof(s->item) ||
       !source || !*source || strlen(source)>=sizeof(s->source))return false;
    strcpy(s->item,item);strcpy(s->source,source);s->ticket=ticket;s->started=now;s->pending=true;
    return true;
}
static inline bool p4_input_binding_ack(p4_input_binding_t *s,const char *item,
    const char *source,unsigned ticket)
{
    if(!s->pending || !item || !source || strcmp(s->item,item) ||
       strcmp(s->source,source) || s->ticket!=ticket)return false;
    s->pending=false;return true;
}
static inline bool p4_input_binding_timeout(const p4_input_binding_t *s,uint64_t now)
{ return s->pending && now>=s->started && now-s->started>=3000; }
/* Data-stream order is not an acknowledgement that a session update has
 * finished applying. Inference must see both its current instructions and
 * the exact appended user item, including after a local tutor selection. */
static inline bool p4_response_dependencies_ready(unsigned sent,unsigned applied,bool input_pending)
{ return sent==applied && !input_pending; }

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
/* An ACK grace interval or queued manual fallback still owns a response.
 * An idle speaker alone is therefore not a connection renewal boundary. */
static inline bool p4_response_settled(const p4_response_owner_t *s)
{ return !s->pending && !s->manual; }
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
/* Queue admission is not a started solve. Only the successfully launched
 * worker's exact current lesson/job can authorize a solving acknowledgement. */
static inline bool p4_tutor_solve_started(unsigned launched_job,unsigned current_job,
    unsigned requested_question,unsigned current_question,unsigned requested_revision,
    unsigned current_revision,const char *phase)
{
    return launched_job && launched_job==current_job && requested_question &&
        requested_question==current_question && requested_revision==current_revision &&
        phase && !strcmp(phase,"solving");
}
typedef enum { P4_TUTOR_LOCAL_NONE, P4_TUTOR_LOCAL_CONFIRM, P4_TUTOR_LOCAL_QUIET } p4_tutor_local_t;
/* A local selection may acknowledge only an actually displayed current page.
 * Requests to read aloud or explain still need their requested content. */
static inline p4_tutor_local_t p4_tutor_local_selection(const char *action,const char *state,
    const char *phase,const char *mode,bool visible,bool rendered,bool quiet_input)
{
    if(!action || !state || !phase || !mode || strcmp(action,"select") ||
       strcmp(state,"succeeded") || !visible || !rendered || !quiet_input)return P4_TUTOR_LOCAL_NONE;
    if(!strcmp(phase,"awaiting_confirmation"))return P4_TUTOR_LOCAL_CONFIRM;
    if(!strcmp(phase,"complete") && !strcmp(mode,"reading"))return P4_TUTOR_LOCAL_QUIET;
    return P4_TUTOR_LOCAL_NONE;
}
/* Connection renewal must not revoke the current photo task. The native
 * stage notice remains scoped to its lesson snapshot and exact cloud job;
 * ordinary tool replies still require their original requesting user. */
static inline bool p4_tutor_stage_owner(bool same_generation,bool same_snapshot,
    unsigned completed_job,unsigned current_job)
{
    return same_snapshot && (same_generation ||
        (completed_job && completed_job==current_job));
}
/* Read/capture operations remain owned by their exact current task after a
 * connection change, including failures before a new question ID is created.
 * This does not grant old explain/read replies another user's authority. */
static inline bool p4_tutor_capture_owner(const char *action,bool same_user,
    unsigned completed_job,unsigned current_job)
{
    return same_user || (action && (!strcmp(action,"start") || !strcmp(action,"refine")) &&
        completed_job && completed_job==current_job);
}
/* The cloud controller can become terminal before its 500 ms completion
 * observer publishes the result. That handoff is still pending work, even
 * though tutor_waiting() has already become false. */
static inline bool p4_tutor_notice_pending(bool tutor,unsigned state,bool sent)
{
    return tutor && state>=1 && state<=3 && !sent;
}
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
/* A claimed completion is not a fresh read/confirmation. Supply only its
 * current spoken content; reader metadata and old progress phrases must not
 * compete with the terminal state. Realtime synthesis still needs physical
 * content checks and is not assumed to be deterministic TTS. */
static inline bool p4_tutor_completion_message(const char *phase,const char *action,
    bool claimed,bool choice,bool visible,bool rendered,char *out,size_t cap)
{
    if(!out || !cap)return false;
    out[0]=0;
    if(!phase || strcmp(phase,"complete") || !action || !claimed ||
       (strcmp(action,"choice") && strcmp(action,"read")))return false;
    const char *prefix="【设备解题完成通知，不是用户发言】解题已真实完成。本次只朗读下面短文本一次，然后停止。不引用历史、不把完成说成读题或等待确认、不重新确认题目、不调用工具。仅朗读：\n";
    bool shown=visible && rendered;
    const char *complete=shown?"解题已经完成，并显示在屏幕上。":"解题已经完成，但屏幕尚未显示解法。";
    const char *tail="";
    if(!strcmp(action,"choice")) {
        if(choice && shown)tail="你想先听讲解，还是先自己看？";
    } else if(shown)tail="你先自己看，有问题再问我。";
    if(strlen(prefix)+strlen(complete)+strlen(tail)+1>cap)return false;
    snprintf(out,cap,"%s%s%s",prefix,complete,tail);return true;
}
typedef enum { P4_TUTOR_NOTICE_SKIP, P4_TUTOR_NOTICE_RESULT, P4_TUTOR_NOTICE_COMPLETION, P4_TUTOR_NOTICE_FAILURE } p4_tutor_notice_t;
/* A first capture can fail before a question ID exists. Only its exact current
 * job and requesting user may receive that failure; zero IDs alone never match. */
static inline bool p4_tutor_capture_failure_current(const char *action,bool same_user,
    bool failed,unsigned old_job,unsigned current_job,const char *phase)
{
    return action && phase && same_user && failed && old_job && old_job==current_job &&
        (!strcmp(action,"start") || !strcmp(action,"refine")) &&
        (!strcmp(phase,"failed") || !strcmp(phase,"needs_clearer_view"));
}
/* A slow synchronous action still owes its caller a result after an accepted
 * receipt. Only the solve/recheck job consumes the lesson completion claim. */
static inline p4_tutor_notice_t p4_tutor_notice_kind(const char *action,bool same_user,
    bool same_snapshot,const char *phase,bool failed,bool completion_pending,bool done)
{
    if(!action || !*action || !phase || !same_snapshot || !strcmp(phase,"cancelled") || !strcmp(phase,"superseded"))return P4_TUTOR_NOTICE_SKIP;
    if(failed && same_user && (!strcmp(action,"start") || !strcmp(action,"refine")) &&
       (!strcmp(phase,"failed") || !strcmp(phase,"needs_clearer_view")))return P4_TUTOR_NOTICE_FAILURE;
    if(!strcmp(action,"confirm") || !strcmp(action,"recheck")) {
        /* Choosing how to study while solving does not revoke failure feedback
         * for this question. A failed recheck can retain a complete old lesson. */
        if(!strcmp(phase,"failed") || !strcmp(phase,"incomplete") || (failed && !strcmp(phase,"complete")))return P4_TUTOR_NOTICE_FAILURE;
        if(!strcmp(phase,"complete"))return completion_pending && !done?P4_TUTOR_NOTICE_COMPLETION:P4_TUTOR_NOTICE_SKIP;
    }
    return same_user?P4_TUTOR_NOTICE_RESULT:P4_TUTOR_NOTICE_SKIP;
}
#endif
