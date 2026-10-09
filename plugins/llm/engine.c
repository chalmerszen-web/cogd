#include "engine.h"
#include <stdio.h>
#include <string.h>

agent_err_t agent_engine_bind_parts(agent_engine_t *e,void *memory,size_t capacity,
                                    void *buffer,size_t buffer_capacity)
{
    uintptr_t a=(uintptr_t)memory,b=(uintptr_t)buffer;
    if(!e || (!memory && (capacity || buffer || buffer_capacity)) || (memory &&
       (!buffer || capacity<sizeof(agent_engine_workspace_t) || buffer_capacity<AGENT_ENGINE_BUFFER_SIZE ||
        a%_Alignof(agent_engine_workspace_t) || b%_Alignof(uint32_t) ||
        a>UINTPTR_MAX-sizeof(agent_engine_workspace_t) || b>UINTPTR_MAX-AGENT_ENGINE_BUFFER_SIZE ||
        (a<b+AGENT_ENGINE_BUFFER_SIZE && b<a+sizeof(agent_engine_workspace_t)))))
        return AGENT_ERR_ARGUMENT;
    if(e->answer_active || e->progress_active || (e->context && e->context->prompt_locked))return AGENT_ERR_BUSY;
    e->workspace=memory;e->buffer=buffer;e->scratch=memory;
    e->scratch_capacity=memory?sizeof(agent_engine_workspace_t):0;
#if AGENT_REQUEST_SCRATCH_COMPACT
    e->preparation_capacity=0;
    e->owned_state_bytes=0;
#endif
    if(e->context) {
        e->context->scratch=buffer;
        e->context->capacity=memory?AGENT_ENGINE_BUFFER_SIZE:0;
    }
    return AGENT_OK;
}

agent_err_t agent_engine_bind_workspace(agent_engine_t *e,void *memory,size_t capacity)
{
    if(!memory)return agent_engine_bind_parts(e,NULL,capacity,NULL,0);
    if(capacity<sizeof(agent_engine_storage_t) || (uintptr_t)memory%_Alignof(agent_engine_storage_t))
        return AGENT_ERR_ARGUMENT;
    agent_engine_storage_t *storage=memory;
    agent_err_t error=agent_engine_bind_parts(e,&storage->work,sizeof(storage->work),
                                            storage->buffer,sizeof(storage->buffer));
    if(!error) {e->scratch=memory;e->scratch_capacity=sizeof(*storage);}
    return error;
}

agent_err_t agent_engine_bind_buffer(agent_engine_t *e,void *buffer,size_t capacity)
{
    uintptr_t p=(uintptr_t)buffer;
    if(!e || !buffer || capacity<AGENT_ENGINE_BUFFER_SIZE || p%_Alignof(uint32_t) ||
       p>UINTPTR_MAX-AGENT_ENGINE_BUFFER_SIZE)return AGENT_ERR_ARGUMENT;
    if(e->workspace || e->scratch || e->answer_active || e->progress_active ||
       (e->context && e->context->prompt_locked))return AGENT_ERR_BUSY;
    e->buffer=buffer;e->scratch_capacity=0;
#if AGENT_REQUEST_SCRATCH_COMPACT
    e->preparation_capacity=0;
    e->owned_state_bytes=0;
#endif
    if(e->context) {e->context->scratch=buffer;e->context->capacity=AGENT_ENGINE_BUFFER_SIZE;}
    return AGENT_OK;
}

#if AGENT_REQUEST_SCRATCH_COMPACT
agent_err_t agent_engine_bind_preparation_buffer(agent_engine_t *e,void *buffer,size_t capacity)
{
    if(capacity>=AGENT_ENGINE_BUFFER_SIZE)return agent_engine_bind_buffer(e,buffer,capacity);
    uintptr_t p=(uintptr_t)buffer;
    if(!e || !buffer || capacity<AGENT_ENGINE_PREPARATION_MIN || p%_Alignof(uint32_t) ||
       p>UINTPTR_MAX-capacity)return AGENT_ERR_ARGUMENT;
    if(e->workspace || e->scratch || e->answer_active || e->progress_active ||
       (e->context && e->context->prompt_locked))return AGENT_ERR_BUSY;
    e->buffer=buffer;e->scratch_capacity=0;e->preparation_capacity=capacity;e->owned_state_bytes=0;
    if(e->context) {e->context->scratch=buffer;e->context->capacity=capacity;}
    return AGENT_OK;
}
#endif

static size_t buffer_capacity(const agent_engine_t *e)
{
#if AGENT_REQUEST_SCRATCH_COMPACT
    if(e->preparation_capacity)return e->preparation_capacity;
#else
    (void)e;
#endif
    return AGENT_ENGINE_BUFFER_SIZE;
}

static agent_err_t answer_begin(void *ctx)
{
    agent_engine_t *e=ctx;
    if(atomic_load(&e->core->cancelled)) return AGENT_ERR_CANCELLED;
    if(!e->answer_active) {
        /* Set before begin so its partially-created resources are joined even
         * if initialization fails. No later request may overwrite the tail. */
        e->answer_active=true;
        agent_err_t error=e->answer_begin?e->answer_begin(e->answer_ctx):AGENT_OK;
        if(error) return error;
    }
    return AGENT_OK;
}

static agent_err_t request_sent(void *ctx)
{
    agent_engine_t *e=ctx;
    if(e->answer_final)return answer_begin(e);
    if(atomic_load(&e->core->cancelled))return AGENT_ERR_CANCELLED;
    if(e->answer_stream && e->answer_prepare && !e->answer_prepared) {
        e->answer_prepared=true;
        agent_err_t error=e->answer_prepare(e->answer_ctx);
        if(error)return error;
    }
    if(e->answer_stream && e->progress_begin && !e->progress_attempted) {
        e->progress_attempted=e->progress_active=true;
        return e->progress_begin(e->progress_ctx);
    }
    return AGENT_OK;
}

static agent_err_t answer_write(agent_engine_t *e,const char *text,size_t length)
{
    agent_err_t started=answer_begin(e);
    if(started) return started;
    while(length) {
        size_t n=length<AGENT_ENGINE_ANSWER_CHUNK?length:AGENT_ENGINE_ANSWER_CHUNK;
        if(n<length) while(n && ((unsigned char)text[n]&0xc0)==0x80) --n;
        if(!n) return AGENT_ERR_PROTOCOL;
        for(unsigned waited=0;;waited+=25) {
            if(atomic_load(&e->core->cancelled)) return AGENT_ERR_CANCELLED;
            agent_err_t error=e->answer_write(e->answer_ctx,text,n);
            if(error!=AGENT_ERR_BUSY) { if(error) return error; break; }
            if(!e->pause_ms || waited>=2000) return AGENT_ERR_TIMEOUT;
            e->pause_ms(25);
        }
        text+=n; length-=n;
    }
    return AGENT_OK;
}

/* A completed, explicitly labelled answer need not be generated a second
 * time. Keep all deltas silent until the complete response proves there was
 * no late tool call. Earlier tool batches have already passed validation,
 * execution and persistence; none is skipped or replayed by this path. */
static bool direct_answer(agent_engine_t *e)
{
    agent_llm_reply_t *reply=&e->workspace->reply;
    if(!reply->done || reply->call_count)return false;
    const char *text=reply->text+strspn(reply->text," \t\r\n");
    if(strncmp(text,"ANSWER:",7))return false;
    text+=7;text+=strspn(text," \t\r\n");
    size_t n=strlen(text);
    while(n && strchr(" \t\r\n",text[n-1]))--n;
    if(!n || (n==5 && !memcmp(text,"READY",5)))return false;
    memmove(reply->text,text,n);reply->text[n]=0;reply->text_length=n;
    return true;
}

static void request_trace(agent_engine_t *,const char *);
static agent_err_t speak_direct_answer(agent_engine_t *e)
{
    /* complete() has released history and joined progress. The response text
     * is disjoint from the speech scratch/queue, and was already counted by
     * emit(); do not double-count it or replay tools/network requests. */
    e->answer_final=true;request_trace(e,"direct_answer");
    agent_llm_reply_t *reply=&e->workspace->reply;
    agent_err_t error=answer_write(e,reply->text,reply->text_length);
    if(!error && e->emit) {
        agent_event_t event={.type=AGENT_EVENT_TEXT,.data=reply->text,.length=reply->text_length};
        error=e->emit(e->emit_ctx,&event);
        if(!error) {event=(agent_event_t){.type=AGENT_EVENT_DONE};error=e->emit(e->emit_ctx,&event);}
    }
    return error;
}

static agent_err_t emit(void *ctx,const agent_event_t *event)
{
    agent_engine_t *e=ctx;
    if(atomic_load(&e->core->cancelled)) return AGENT_ERR_CANCELLED;
    if(e->answer_final && (event->type==AGENT_EVENT_TOOL_BEGIN || event->type==AGENT_EVENT_TOOL_ARGUMENT ||
                          event->type==AGENT_EVENT_TOOL_END)) return AGENT_ERR_PROTOCOL;
    if(event->type==AGENT_EVENT_TEXT && event->length) {
        if(event->length>AGENT_ANSWER_MAX-e->total_text) return AGENT_ERR_LIMIT;
        e->total_text+=event->length; e->output_started=true;
    }
    /* A tool-capable response may contain text before a late tool call. Keep
     * that text for protocol pairing, but never send it to speech/output as a
     * final answer. READY is an internal planning result, not conversation. */
    if(e->answer_stream && !e->answer_final &&
       (event->type==AGENT_EVENT_TEXT || event->type==AGENT_EVENT_DONE)) return AGENT_OK;
    agent_err_t error=AGENT_OK;
    /* The speech producer must not wait behind optional USB mirroring. Do not
     * mirror text that the speech sink failed to accept. */
    if(e->answer_final && event->type==AGENT_EVENT_TEXT && event->length)
        error=answer_write(e,event->data,event->length);
    if(!error && e->emit) error=e->emit(e->emit_ctx,event);
    return error;
}
static agent_err_t feed(void *ctx,const char *data,size_t n)
{
    agent_engine_t *e=ctx;
    if(n>AGENT_REQUEST_MAX-e->used) return AGENT_ERR_LIMIT;
    memcpy(e->buffer+e->used,data,n); e->used+=n; e->buffer[e->used]=0; return AGENT_OK;
}
static bool transient(agent_err_t error)
{
    return error==AGENT_ERR_NETWORK || error==AGENT_ERR_DNS || error==AGENT_ERR_TIMEOUT ||
        error==AGENT_ERR_OFFLINE || error==AGENT_ERR_SERVER || error==AGENT_ERR_RATE;
}
static bool trim_history(agent_engine_t *e,size_t *turn_start,size_t snapshot_bytes)
{
    size_t keep=*turn_start-snapshot_bytes;
    if(keep<=e->workspace->messages.system_used) return false;
    size_t removed=keep-e->workspace->messages.system_used;
    memmove(e->workspace->messages.data+e->workspace->messages.system_used,e->workspace->messages.data+keep,e->workspace->messages.used-keep+1);
    e->workspace->messages.used-=removed; *turn_start=e->workspace->messages.system_used+snapshot_bytes; return true;
}
static bool result_needs_plan(const char *result)
{
    /* A model's last-batch hint is conditional on real results. Empty recall,
     * rejected work and malformed/unknown results retain the planning path. */
    cJSON *root=agent_json_parse(result,strlen(result));
    bool more=!cJSON_IsObject(root) || !root->child;
    const char *flags[]={"executed","ok","success","valid","available","present"};
    for(unsigned i=0;i<sizeof(flags)/sizeof(*flags);++i)
        if(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root,flags[i])))more=true;
    const cJSON *error=cJSON_GetObjectItemCaseSensitive(root,"error");
    if(error && !cJSON_IsNull(error) && !cJSON_IsFalse(error) &&
       !(cJSON_IsString(error) && (!error->valuestring[0] || !strcmp(error->valuestring,"ok"))))more=true;
    const cJSON *hits=cJSON_GetObjectItemCaseSensitive(root,"hits");
    if(hits && (!cJSON_IsArray(hits) || !hits->child))more=true;
    if(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"pending")) ||
       cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"active")))more=true;
    cJSON_Delete(root);return more;
}
static agent_err_t assistant(agent_engine_t *e,size_t *turn_start,size_t snapshot_bytes)
{
    agent_err_t error=agent_messages_assistant(&e->workspace->messages,&e->workspace->reply);
    if(error==AGENT_ERR_LIMIT && trim_history(e,turn_start,snapshot_bytes)) error=agent_messages_assistant(&e->workspace->messages,&e->workspace->reply);
    return error;
}

/* A complete invalid batch has no side effects. Return every original call ID so
 * the model can correct it, without replaying a turn or losing protocol pairing. */
static agent_err_t repair_batch(agent_engine_t *e,size_t *turn_start,size_t snapshot_bytes)
{
    agent_err_t error=assistant(e,turn_start,snapshot_bytes);
    for(unsigned i=0;!error && i<e->workspace->reply.call_count;++i) {
        char detail[384],result[640];
        agent_err_t check=agent_tool_check(e->workspace->reply.calls[i].name,agent_call_arguments(&e->workspace->reply,i),detail,sizeof(detail));
        if(!check) snprintf(detail,sizeof(detail),"Another call in this batch was invalid; nothing in this batch was executed. Submit the corrected batch.");
        agent_json_writer_t w;agent_json_writer_init(&w,result,sizeof(result));
        agent_json_printf(&w,"{\"executed\":false,\"error\":\"%s\",\"detail\":",check?agent_err_name(check):"batch_rejected");
        agent_json_quote(&w,detail);agent_json_raw(&w,"}");error=w.error;
        if(!error) {
            agent_event_t event={.type=AGENT_EVENT_TOOL_END,.data=result,.length=w.used,.index=i};
            error=emit(e,&event);
        }
        if(!error) {
            error=agent_messages_add(&e->workspace->messages,"tool",result,e->workspace->reply.calls[i].id);
            if(error==AGENT_ERR_LIMIT && trim_history(e,turn_start,snapshot_bytes)) error=agent_messages_add(&e->workspace->messages,"tool",result,e->workspace->reply.calls[i].id);
        }
    }
    return error;
}
static bool repairable_batch(const agent_llm_reply_t *reply)
{
    if(!reply->done || !reply->call_count || reply->call_count>AGENT_TOOLS_MAX) return false;
    for(unsigned i=0;i<reply->call_count;++i) {
        if(!reply->calls[i].id[0] || !agent_tool_find(reply->calls[i].name)) return false;
        for(unsigned j=0;j<i;++j) if(!strcmp(reply->calls[i].id,reply->calls[j].id)) return false;
    }
    return true;
}
static agent_err_t gateway_request(agent_engine_t *e,bool stream,unsigned round,size_t turn_start,size_t *length)
{
    agent_json_writer_t w; agent_json_writer_init(&w,e->buffer,AGENT_ENGINE_BUFFER_SIZE);
    agent_json_raw(&w,"{\"schema\":\"agent.turn/1\",\"device_id\":"); agent_json_quote(&w,e->context->device);
    agent_json_raw(&w,",\"user_id\":"); agent_json_quote(&w,e->context->user);
    agent_json_raw(&w,",\"session_id\":"); agent_json_quote(&w,e->context->session);
    agent_json_raw(&w,",\"turn_id\":"); agent_json_quote(&w,e->turn_id);
    agent_json_printf(&w,",\"stream\":%s,\"round\":%u,\"messages\":[",stream?"true":"false",round);
    /* Only this turn goes through the gateway; it owns cloud prompt assembly. */
    agent_json_raw(&w,e->workspace->messages.data+turn_start);
    agent_json_raw(&w,",\"capabilities\":");
    agent_err_t error=agent_tools_write(NULL,agent_json_write_bytes,&w);
    if(error) return error;
    agent_json_raw(&w,"}");
    *length=w.used; return w.error;
}

static bool long_history(const agent_engine_t *e)
{
    const agent_context_ops_t *c=e->context_ops;
    return !e->gateway && e->llm->compose && c->prompt && c->select && c->replay && c->release;
}

typedef struct {
    agent_engine_deferred_t *owner;
    const char *input;
    char *buffer;
    bool compact;
    size_t snapshots,prefix_bytes;
    uint64_t generation,next_seq;
    bool resumed,prepared,selection_owned;
    agent_err_t result;
} initial_request_t;
typedef struct {
    agent_engine_t *engine;
    initial_request_t *initial;
    bool stream,omit_history,history_released;
} request_body_t;
static agent_err_t initial_write(void *,agent_write_fn,void *);

static void request_trace(agent_engine_t *e,const char *phase)
{ if(e->request_trace) e->request_trace(e->request_trace_ctx,phase); }

static agent_err_t write_messages(void *ctx,agent_write_fn write,void *write_ctx)
{
    const request_body_t *body=ctx;
    agent_engine_t *e=body->engine;
    agent_err_t error;
    if(body->initial && !body->initial->resumed)error=initial_write(ctx,write,write_ctx);
    else {
        size_t length=e->workspace->messages.used-(e->answer_stream?1u:0u);
        if(!long_history(e)) error=write(write_ctx,e->workspace->messages.data,length);
        else {
            size_t boundary=e->workspace->messages.system_used-1;
            error=write(write_ctx,e->workspace->messages.data,boundary);
            if(!error && !body->omit_history) error=e->context_ops->replay(e->context,write,write_ctx);
            if(!error) error=write(write_ctx,e->workspace->messages.data+boundary,length-boundary);
        }
    }
    if(!error && e->answer_stream) {
        if(e->answer_final && e->progress_text && *e->progress_text) {
            /* The short acknowledgement was actually heard, but is neither
             * a completed answer nor durable conversation history. */
            char acknowledgement[768];agent_json_writer_t w;
            agent_json_writer_init(&w,acknowledgement,sizeof(acknowledgement));
            agent_json_raw(&w,",{\"role\":\"assistant\",\"content\":");
            agent_json_quote(&w,e->progress_text);agent_json_raw(&w,"}");
            if(e->progress_language && *e->progress_language)
                agent_json_raw(&w,!strcmp(e->progress_language,"yue")?
                    ",{\"role\":\"system\",\"content\":\"Continue this spoken answer in Cantonese.\"}":
                    ",{\"role\":\"system\",\"content\":\"Continue this spoken answer in Mandarin.\"}");
            error=w.error?w.error:write(write_ctx,acknowledgement,w.used);
            if(error)return error;
        }
        static const char planning_prompt[]=",{\"role\":\"system\",\"content\":\"This is the planning stage. Run required tools. When current facts or actual tool results suffice to answer with no further calls, return ANSWER: followed by the complete spoken answer, no planning commentary. Never invent success or treat pending work as completed. Otherwise return exactly READY for separate final speech. Set final_batch=true only on the last call if successful results suffice to answer; omit for dependent actions/checks or async completion. The flag is not proof of success.\"}]";
        static const char final_prompt[]=",{\"role\":\"system\",\"content\":\"Planning is complete. Return only a short plain spoken sentence from the actual tool results; never invent success. status.light is already a fresh color reading after the plan, so use it directly without another check. Continue the heard acknowledgement in its language without repeating greetings, questions or promises. Tools are disabled: never write or imitate a tool call, DSML/XML tags, function names, READY, or planning commentary.\"}]";
        static const char clarify_prompt[]=",{\"role\":\"system\",\"content\":\"ASR lacks details. Ask one short question; never guess from history or claim action.\"}]";
        static const char budget_prompt[]=",{\"role\":\"system\",\"content\":\"Tool-call budget reached. Tools are disabled. Return only a short plain spoken sentence from actual results. If a job is active/pending, say it is still running and completion is unconfirmed; never claim success, restart it, or promise a later notification. Explain any failed or unverified part. Never write or imitate a tool call, DSML/XML tags, function names, READY, or planning commentary.\"}]";
        /* Phase directives are wire-only and mutually exclusive. The final
         * request never contains an earlier instruction to output READY. */
        error=e->clarify_only?write(write_ctx,clarify_prompt,sizeof(clarify_prompt)-1):
              e->answer_final && e->core->tool_rounds>=AGENT_ROUNDS_MAX?write(write_ctx,budget_prompt,sizeof(budget_prompt)-1):
              e->answer_final?write(write_ctx,final_prompt,sizeof(final_prompt)-1):
                              write(write_ctx,planning_prompt,sizeof(planning_prompt)-1);
    }
    return error;
}

static agent_err_t system_prompt(agent_engine_t *e)
{
    const char *base_prompt=
        "ESP-Hi agent. Brief replies in user's language; omit hardware state for greetings/general/memory queries. "
        "Use this turn's live snapshots or query tools before claiming hardware state; later tool results override snapshots, history isn't live. Execute/check requests. "
        "executed=false: fix/retry this turn, never claim success; <=4 tools/response. "
        "Search missing history. Summaries: durable user facts/decisions, no hardware status/capabilities; preserve other user facts. If already saved, acknowledge without rewriting. Never invent memories; new input overrides summaries/guesses. "
        "Discover GPIO capabilities. device_gpio_set persists input/output/PWM; device_gpio_get reads back. Control plans time actions then restore outputs. "
        "Admission isn't completion: check status. Finished plans restore prior outputs; status.light is the live color, not the last planned step. Release mic/playback before plans needing them."
#if AGENT_ENABLE_AUDIO
        " Music: device_audio_play_song, four voices, <=75s. One minute=16 two-bar rows at128BPM. Reuse6 patterns: "
        "0/1 lead variants:8 notes each,ticks=4;2/3 bass variants:2 notes each,ticks=16;4 arpeggio,5 drums:8 notes each,ticks=4. "
        "Vary entrances/gain, phrasing/dynamics, pitches/velocities/gates. Harmonize lead/bass/arp; end on tonic with tonic bass. "
        "Require32 ticks(two bars), not64, per pattern; valid zero-based references. Separate parts; backing fills32 ticks. "
        "Compact args/replies. play_score: short mono cue; check audio status for completion. "
        "mic_valid=true: fresh level, not ASR. Local capture/replay needs clip_storage=true and prior light cue. "
        "Voice mode transcribes endpointed audio and speaks replies."
#endif
    ;
    /* Context copies the system text into messages before reusing its scratch.
     * Reuse the idle HTTP buffer, not another resident prompt allocation. */
    size_t capacity=buffer_capacity(e);
    int prompt_size=snprintf(e->buffer,capacity,"%s\n%s\n%s",base_prompt,
        e->tools->hardware_prompt?e->tools->hardware_prompt:"",
        e->voice_mode?"ASR input, TTS output. User's language; one sentence, <=40 Chinese chars unless detail requested. No Markdown/URLs/raw RGB/tool JSON. Use tools; never capture/replay the reserved mic clip.":"");
    return prompt_size<0 || (size_t)prompt_size>=capacity?AGENT_ERR_LIMIT:AGENT_OK;
}

typedef agent_err_t (*snapshot_fn)(void *,const char *);
static agent_err_t snapshot_message(void *ctx,const char *text)
{ return agent_messages_add(ctx,"system",text,NULL); }
static agent_err_t snapshot_save(void *ctx,const char *text)
{
    agent_json_writer_t *w=ctx;
    agent_json_raw(w,",");
    return w->error?w->error:agent_message_write("system",text,NULL,agent_json_write_bytes,w);
}
static agent_err_t display_snapshot(agent_engine_t *e,uint64_t now,snapshot_fn add,void *ctx)
{
    if(!e->tools->display || !e->tools->display->status) return AGENT_OK;
    size_t capacity=buffer_capacity(e);
    int n=snprintf(e->buffer,capacity,"Current-turn display snapshot (device uptime %llu ms): ",
        (unsigned long long)now);
    if(n<0 || (size_t)n>=capacity) return AGENT_ERR_LIMIT;
    agent_err_t state=e->tools->display->status(e->tools->display->ctx,e->buffer+n,capacity-(size_t)n);
    if(state) snprintf(e->buffer+n,capacity-(size_t)n,
        "unavailable: %s. Query before claiming any current state.",agent_err_name(state));
    return add(ctx,e->buffer);
}
static agent_err_t light_snapshot(agent_engine_t *e,uint64_t now,snapshot_fn add,void *ctx)
{
    if(!e->tools->light_get)return AGENT_OK;
    uint8_t rgb[3];agent_err_t state=e->tools->light_get(e->tools->ctx,rgb);
    size_t capacity=buffer_capacity(e);
    int n=snprintf(e->buffer,capacity,"Current-turn light snapshot (device uptime %llu ms): ",
        (unsigned long long)now);
    if(n<0 || (size_t)n>=capacity)return AGENT_ERR_LIMIT;
    if(state)snprintf(e->buffer+n,capacity-(size_t)n,
        "unavailable: %s. Query before claiming current light state.",agent_err_name(state));
    else snprintf(e->buffer+n,capacity-(size_t)n,
        "{\"r\":%u,\"g\":%u,\"b\":%u}",rgb[0],rgb[1],rgb[2]);
    return add(ctx,e->buffer);
}

/* No state workspace is read by this writer. Both size measurement and actual
 * transmission use the same frozen hardware snapshot and immutable full ASR. */
static agent_err_t initial_write(void *ctx,agent_write_fn write,void *write_ctx)
{
    request_body_t *body=ctx;agent_engine_t *e=body->engine;
    initial_request_t *first=body->initial;
    agent_err_t error=system_prompt(e);
    if(!error)error=agent_context_write_prefix(e->context,e->buffer,write,write_ctx);
    if(!error && !body->omit_history)error=e->context_ops->replay(e->context,write,write_ctx);
    if(!error)error=write(write_ctx,first->owner->snapshots,first->snapshots);
    if(!error)error=write(write_ctx,",",1);
    if(!error)error=agent_message_write("user",first->input,NULL,write,write_ctx);
    if(!error && !e->answer_stream)error=write(write_ctx,"]",1);
    return error;
}

static agent_err_t initial_resume(agent_engine_t *e,initial_request_t *first,agent_err_t result)
{
    if(!first || first->resumed)return result?result:first?first->result:AGENT_OK;
    first->resumed=true;
    if(first->selection_owned) {
        e->context_ops->release(e->context);first->selection_owned=false;
    }
    request_trace(e,"workspace_handoff_begin");
    agent_err_t resumed=first->owner->resume(first->owner->ctx,result);
    first->result=result?result:resumed;
    if(!first->result && (!e->workspace || !e->buffer ||
       buffer_capacity(e)!=AGENT_ENGINE_BUFFER_SIZE || (!first->compact && e->buffer!=first->buffer)))
        first->result=AGENT_ERR_MEMORY;
    if(!first->result && first->prepared &&
       (e->context->wal.generation!=first->generation || e->context->wal.next_seq!=first->next_seq))
        first->result=AGENT_ERR_BUSY;
    request_trace(e,"workspace_handoff_end");
    return first->result;
}

static agent_err_t initial_rebuild(agent_engine_t *e,initial_request_t *first)
{
    agent_err_t error=system_prompt(e);
    agent_messages_t *m=&e->workspace->messages;
    if(!error)error=agent_context_prompt(e->context,m,e->buffer);
    if(!error && m->used!=first->prefix_bytes+1)error=AGENT_ERR_PROTOCOL;
    if(!error) {
        agent_json_writer_t w;
        agent_json_writer_init(&w,m->data+m->used-1,sizeof(m->data)-m->used+1);
        agent_json_raw(&w,first->owner->snapshots);agent_json_raw(&w,"]");
        error=w.error;
        if(!error) {m->used+=first->snapshots;error=agent_messages_add(m,"user",first->input,NULL);}
    }
    return error;
}

static agent_err_t initial_count(void *ctx,const char *data,size_t length)
{
    (void)data;size_t *size=ctx;
    if(length>AGENT_HISTORY_MAX-*size)return AGENT_ERR_LIMIT;
    *size+=length;return AGENT_OK;
}
static agent_err_t initial_measure(agent_engine_t *e,initial_request_t *first)
{
    size_t bytes=0;
    agent_err_t error=system_prompt(e);
    if(!error)error=agent_context_write_prefix(e->context,e->buffer,initial_count,&bytes);
    first->prefix_bytes=bytes;
    if(!error)error=initial_count(&bytes,NULL,first->snapshots+2); /* comma + final ']' */
    if(!error)error=agent_message_write("user",first->input,NULL,initial_count,&bytes);
    if(!error)first->prepared=true;
    return error;
}

static agent_err_t response_ready(request_body_t *body,agent_err_t result)
{
    initial_request_t *first=body->initial;
    if(!first || first->resumed)return result?result:first?first->result:AGENT_OK;
    agent_engine_t *e=body->engine;
    result=initial_resume(e,first,result);body->history_released=true;
    if(!result)result=initial_rebuild(e,first);
    first->result=result;
    if(!result) {
        agent_llm_reply_init(&e->workspace->reply);
        agent_sse_init(&e->workspace->sse,e->buffer,e->answer_stream?AGENT_STREAM_MAX+1:AGENT_ENGINE_BUFFER_SIZE,
                       &e->workspace->reply,emit,e);
        e->used=0;
    }
    return result;
}
static agent_err_t body_sent(void *ctx)
{
    request_body_t *body=ctx;
    agent_err_t error=response_ready(body,AGENT_OK);
    return error?error:request_sent(body->engine);
}
static agent_err_t response_feed(void *ctx,const char *data,size_t size)
{
    request_body_t *body=ctx;
    agent_err_t error=response_ready(body,AGENT_OK);
    return error?error:body->stream?agent_sse_feed(&body->engine->workspace->sse,data,size):feed(body->engine,data,size);
}

static agent_err_t write_request(void *ctx,agent_write_fn write,void *write_ctx)
{
    const request_body_t *body=ctx;
    if(body->engine->answer_final)
        return body->engine->llm->compose_final(body->stream,write_messages,ctx,write,write_ctx);
    return body->engine->llm->compose(body->stream,write_messages,ctx,
        body->engine->answer_stream?agent_tools_voice_write:agent_tools_write,NULL,write,write_ctx);
}

static agent_err_t complete(agent_engine_t *e,bool stream,size_t turn_start,initial_request_t *first)
{
    agent_err_t error=AGENT_OK;
    bool streaming=long_history(e);
    request_body_t body={.engine=e,.stream=stream,.initial=first};
    for(unsigned attempt=0;attempt<2;++attempt) {
        error=AGENT_OK;
        size_t length=0;
        e->context->request_bytes=0;
        request_trace(e,"select_begin");
        if(streaming) {
            error=e->context_ops->select(e->context,e->history_before,&e->core->cancelled);
            if(error) return error;
            if(first)first->selection_owned=true;
        }
        request_trace(e,"selected");
        /* Selection has already serialized and CRC-checked the locked turns.
         * Count only the envelope here, adding that exact history byte count.
         * Custom composers/replayers retain the general two-pass contract.
         * Actual transmission still replays/checks every record and validates
         * Content-Length; no history is removed and no new buffer is needed. */
        body.omit_history=streaming &&
            e->context_ops->select==agent_context_select && e->context_ops->replay==agent_context_replay &&
            (e->answer_final?e->llm->compose_final==agent_deepseek_compose_final:
                             e->llm->compose==agent_deepseek_compose);
        if(!error) error=e->gateway?gateway_request(e,stream,e->core->tool_rounds,turn_start,&length):
            agent_http_measure_body(write_request,&body,&e->core->cancelled,&length);
        if(!error && body.omit_history) {
            if(!e->context->prompt_locked || e->context->prompt_generation!=e->context->wal.generation)
                error=AGENT_ERR_BUSY;
            else if(e->context->prompt_bytes>AGENT_HTTP_REQUEST_MAX-length) error=AGENT_ERR_LIMIT;
            else length+=e->context->prompt_bytes;
        }
        body.omit_history=false;
        if(error) {
            if(streaming)e->context_ops->release(e->context);
            if(first)first->selection_owned=false;
            return error;
        }
        e->context->request_bytes=length;
        request_trace(e,"measured");
        if(!first) {
            agent_llm_reply_init(&e->workspace->reply);
            agent_sse_init(&e->workspace->sse,e->buffer,e->answer_stream?AGENT_STREAM_MAX+1:AGENT_ENGINE_BUFFER_SIZE,&e->workspace->reply,emit,e);
        }
        agent_http_request_t request={.target=e->gateway?AGENT_HTTP_GATEWAY:AGENT_HTTP_DEEPSEEK,
            .path=e->gateway?"/v1/agent/turns":"/chat/completions",.method="POST",.body=e->gateway?e->buffer:NULL,
            .length=length,.cancelled=&e->core->cancelled,.produce=e->gateway?NULL:write_request,.body_ctx=&body,
            .reuse_connection=e->voice_mode && !e->gateway,
            .on_sent=e->answer_stream?body_sent:NULL,.sent_ctx=&body,
            .trace=e->request_trace,.trace_ctx=e->request_trace_ctx};
        e->used=0;
        error=e->transport->perform(e->transport->ctx,&request,response_feed,&body);
        error=response_ready(&body,error);
        request_trace(e,"response_done");
        if(e->progress_active) {
            agent_err_t joined=e->progress_end(e->progress_ctx,error);
            e->progress_active=false;
            if(!error)error=joined;
        }
        if(streaming && !body.history_released) e->context_ops->release(e->context);
        if(!error) error=stream?agent_sse_finish(&e->workspace->sse):e->llm->parse(&e->workspace->reply,e->buffer,e->used,false,emit,e);
        if(!error || first || !transient(error) || e->output_started || e->effects || e->answer_active || e->answer_prepared || e->progress_attempted || attempt || !e->pause_ms) break;
        unsigned delay=500+(e->random_u32?e->random_u32()%500:0);
        for(unsigned waited=0;waited<delay;waited+=25) {
            if(atomic_load(&e->core->cancelled)) return AGENT_ERR_CANCELLED;
            e->pause_ms(25);
        }
    }
    return error;
}
/* The assistant's tool-call text is already copied into messages. Reuse that
 * now-idle text region until the next complete() resets the reply. Arguments
 * and call IDs remain disjoint; WAL scratch and message storage stay untouched.
 * The caller validated the whole batch and joined borrowed speech buffers. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static agent_err_t execute_batch(agent_engine_t *e,size_t *turn_start,
                                 size_t snapshot_bytes,bool last_batch)
{
    enum { RESULT_BYTES=2048, CONTENT_BYTES=2560 };
    _Static_assert(RESULT_BYTES+CONTENT_BYTES<=sizeof(e->workspace->reply.text),"Tool scratch must fit idle reply text");
    char *result=e->workspace->reply.text,*content=result+RESULT_BYTES;
    for(unsigned i=0;i<e->workspace->reply.call_count;++i) {
        if(atomic_load(&e->core->cancelled)) return AGENT_ERR_CANCELLED;
        const agent_tool_call_t *call=&e->workspace->reply.calls[i];
        agent_json_writer_t w;
        e->effects=true;
        agent_err_t error=agent_tool_invoke(e->tools,call->name,agent_call_arguments(&e->workspace->reply,i),result,RESULT_BYTES);
        if(error) return error;
        if(last_batch && result_needs_plan(result))last_batch=false;
        agent_event_t event={.type=AGENT_EVENT_TOOL_END,.data=result,.length=strlen(result),.index=i};
        error=emit(e,&event);
        if(error) return error;
        agent_json_writer_init(&w,content,CONTENT_BYTES);
        agent_json_raw(&w,"{\"tool\":");
        const agent_tool_description_t *tool=agent_tool_find(call->name);
        agent_json_quote(&w,tool->name);
        agent_json_raw(&w,",\"call_id\":"); agent_json_quote(&w,call->id);
        agent_json_raw(&w,",\"turn_id\":"); agent_json_quote(&w,e->turn_id);
        agent_json_printf(&w,",\"result\":%s}",result);
        if(w.error) return w.error;
        error=e->context_ops->append(e->context,"tool_result","tool",content,NULL,0);
        if(error) return error;
        error=agent_messages_add(&e->workspace->messages,"tool",result,call->id);
        if(error==AGENT_ERR_LIMIT && trim_history(e,turn_start,snapshot_bytes)) error=agent_messages_add(&e->workspace->messages,"tool",result,call->id);
        if(error) return error;
    }
    if(last_batch) {
        /* Keep all real call/result pairs; omit only a redundant READY round. */
        request_trace(e,"batch_final");e->answer_final=true;
    }
    return AGENT_OK;
}

static agent_err_t turn(agent_engine_t *e,const char *input,bool stream,initial_request_t *first)
{
    if(!input || !*input || strlen(input)>AGENT_INPUT_MAX || !agent_utf8_valid(input,strlen(input))) return AGENT_ERR_LIMIT;
    if(!e || (!e->workspace && !(first && e->buffer)))return AGENT_ERR_MEMORY;
    if(!e->core || !e->transport || !e->llm || !e->tools || !e->context || !e->context->wal.ready) return AGENT_ERR_CONFIG;
    if(!e->gateway && !e->llm->compose) return AGENT_ERR_CONFIG;
    e->answer_stream=e->voice_mode && stream && !e->gateway && e->answer_write;
    if(e->clarify_only && !e->answer_stream)return AGENT_ERR_CONFIG;
    e->answer_final=e->clarify_only;e->answer_active=e->answer_prepared=false;
    e->progress_active=false;
    e->progress_text=e->answer_stream?e->heard_ack:NULL;
    e->progress_language=e->progress_text?e->heard_ack_language:NULL;
    e->progress_attempted=e->progress_text && *e->progress_text;
    if(e->answer_stream && (!e->llm->compose_final || !e->answer_end)) return AGENT_ERR_CONFIG;
    if(e->answer_stream && e->progress_begin && !e->progress_end)return AGENT_ERR_CONFIG;
    if(!e->context_ops) e->context_ops=&agent_context_ops;
    if(first && (e->workspace || !e->answer_stream ||
       e->context_ops->append!=agent_context_emit || e->context_ops->prompt!=agent_context_prompt ||
       e->context_ops->select!=agent_context_select || e->context_ops->replay!=agent_context_replay ||
       e->context_ops->release!=agent_context_release)) {
        agent_err_t restored=initial_resume(e,first,AGENT_OK);
        if(restored)return restored;
        first=NULL;
    }
    uint64_t now=e->tools->now_ms(e->tools->ctx);
    if(now<e->circuit_until) return AGENT_ERR_BUSY;
    e->output_started=false; e->effects=false; e->total_text=0;
    agent_err_t error=AGENT_OK;
    if(first) {
        if(first->compact) {
            /* Check every preparation writer before committing the user
             * event. Large system/facts/summary data takes the full path. */
            error=initial_measure(e,first);
            first->prepared=false;
            if(error==AGENT_ERR_LIMIT) {
                error=initial_resume(e,first,AGENT_OK);first=NULL;
            }
            if(error)return error;
        }
    }
    if(first) {
        agent_json_writer_t snapshots;
        agent_json_writer_init(&snapshots,first->owner->snapshots,sizeof(first->owner->snapshots));
        error=display_snapshot(e,now,snapshot_save,&snapshots);
        if(!error)error=light_snapshot(e,now,snapshot_save,&snapshots);
        first->snapshots=snapshots.used;
        if(error==AGENT_ERR_LIMIT) {
            /* Optional compact staging never lowers the ordinary adapter's
             * capacity. No user event/request has been submitted at this point. */
            error=initial_resume(e,first,AGENT_OK);first=NULL;
        }
        if(error)return error;
    }
    if(e->context_ops->append==agent_context_emit) {
        /* Native persistence can quote directly into its event envelope;
         * it does not need to keep a second copy in the messages workspace. */
        error=agent_context_emit_user(e->context,input,e->turn_id,sizeof(e->turn_id));
    } else {
        agent_json_writer_t w;agent_json_writer_init(&w,e->workspace->messages.data,sizeof(e->workspace->messages.data));
        agent_json_raw(&w,"{\"format\":\"text/plain\",\"text\":");agent_json_quote(&w,input);agent_json_raw(&w,"}");
        error=w.error;
        if(!error)error=e->context_ops->append(e->context,"message","user",e->workspace->messages.data,e->turn_id,sizeof(e->turn_id));
    }
    if(error) return error;
    e->history_before=e->context->wal.next_seq;
    agent_err_t (*history)(agent_context_t *,agent_messages_t *,const char *)=
        long_history(e)?e->context_ops->prompt:e->context_ops->history;
    size_t turn_start=0,snapshot_bytes=0;
    if(first) {
        first->generation=e->context->wal.generation;first->next_seq=e->context->wal.next_seq;
        error=initial_measure(e,first);
        snapshot_bytes=first->snapshots;turn_start=first->prefix_bytes+1+snapshot_bytes;
    } else {
        error=system_prompt(e);
        if(!error)error=history(e->context,&e->workspace->messages,e->buffer);
        /* Keep changing uptime/state after replayed history so an identical
         * long prompt retains its cacheable prefix. Snapshots are not durable. */
        size_t history_end=e->workspace->messages.used;
        if(!error)error=display_snapshot(e,now,snapshot_message,&e->workspace->messages);
        if(!error)error=light_snapshot(e,now,snapshot_message,&e->workspace->messages);
        turn_start=e->workspace->messages.used;snapshot_bytes=turn_start-history_end;
        if(!error)error=agent_messages_add(&e->workspace->messages,"user",input,NULL);
    }
    unsigned repairs=0;
    while(!error) {
        /* Finish within the existing action budget. Never solicit a fifth
         * batch only to abort after the prior four have already taken effect. */
        if(e->answer_stream && !e->answer_final && e->core->tool_rounds>=AGENT_ROUNDS_MAX) {
            e->answer_final=true;request_trace(e,"round_budget_final");
        }
        error=complete(e,stream,turn_start,first);
        error=initial_resume(e,first,error);first=NULL;
        if(error) break;
        if(!e->workspace->reply.call_count) {
            if(e->answer_stream && !e->answer_final) {
                if(direct_answer(e)) {
                    error=speak_direct_answer(e);
                    if(!error)error=assistant(e,&turn_start,snapshot_bytes);
                    break;
                }
                /* Do not persist an internal READY answer or spend one of the
                 * four tool rounds on the final, tools-disabled response. */
                e->answer_final=true;
                continue;
            }
            error=assistant(e,&turn_start,snapshot_bytes); break;
        }
        error=agent_next_round(e->core);
        if(error) break;
        bool last_batch=false;
        if(e->answer_stream) {
            error=agent_tools_voice_hint(&e->workspace->reply,e->buffer,AGENT_ENGINE_BUFFER_SIZE,&last_batch);
            /* A malformed envelope cannot enter repair/history with internal
             * metadata still attached. No call in this batch has executed. */
            if(error)break;
        }
        error=agent_tools_validate(&e->workspace->reply);
        if(error && repairs<2 && !e->effects && repairable_batch(&e->workspace->reply) &&
           (error==AGENT_ERR_ARGUMENT || error==AGENT_ERR_JSON || error==AGENT_ERR_LIMIT || error==AGENT_ERR_DUPLICATE)) {
            ++repairs;error=repair_batch(e,&turn_start,snapshot_bytes);
            if(!error) continue;
        }
        if(!error && e->before_tools) error=e->before_tools(e->before_tools_ctx);
        if(!error && atomic_load(&e->core->cancelled)) error=AGENT_ERR_CANCELLED;
        if(!error) error=assistant(e,&turn_start,snapshot_bytes);
        if(!error) error=execute_batch(e,&turn_start,snapshot_bytes,last_batch);
    }
    error=initial_resume(e,first,error);
    if(e->answer_active) {
        /* context->scratch aliases buffer. The sink must finish/cancel and
         * release its borrowed tail before any WAL serialization below. */
        agent_err_t ended=e->answer_end(e->answer_ctx,error);
        e->answer_active=false;
        if(!error) error=ended;
    }
    if(!error) {
        size_t n=strlen(e->workspace->messages.data+turn_start);
        if(n+15>=sizeof(e->workspace->messages.data)) error=AGENT_ERR_LIMIT;
        else {
            memmove(e->workspace->messages.data+13,e->workspace->messages.data+turn_start,n);
            memcpy(e->workspace->messages.data,"{\"messages\":[",13);
            e->workspace->messages.data[13+n]='}'; e->workspace->messages.data[14+n]=0;
            error=e->context_ops->append(e->context,"turn","assistant",e->workspace->messages.data,NULL,0);
        }
    }
    if(error) {
        char content[192]; snprintf(content,sizeof(content),"{\"turn_id\":\"%s\",\"error\":\"%s\",\"partial\":%s,\"effects\":%s}",
            e->turn_id,agent_err_name(error),e->output_started?"true":"false",e->effects?"true":"false");
        (void)e->context_ops->append(e->context,"error","system",content,NULL,0);
        if(transient(error)) {
            if(e->failures<3) ++e->failures;
            if(e->failures>=3) e->circuit_until=e->tools->now_ms(e->tools->ctx)+30000;
        } else e->failures=0;
    } else e->failures=0;
    return error;
}

agent_err_t agent_engine_turn(agent_engine_t *e,const char *input,bool stream)
{ return turn(e,input,stream,NULL); }

agent_err_t agent_engine_turn_deferred(agent_engine_t *e,const char *input,bool stream,
                                      agent_engine_deferred_t *deferred)
{
    if(!e || !deferred || !deferred->resume)return AGENT_ERR_ARGUMENT;
    initial_request_t first={.owner=deferred,.input=input,.buffer=e->buffer,
        .compact=buffer_capacity(e)<AGENT_ENGINE_BUFFER_SIZE};
    agent_err_t error=turn(e,input,stream,&first);
    return initial_resume(e,&first,error);
}
