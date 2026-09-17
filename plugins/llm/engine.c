#include "engine.h"
#include <stdio.h>
#include <string.h>

static agent_err_t emit(void *ctx,const agent_event_t *event)
{
    agent_engine_t *e=ctx;
    if(atomic_load(&e->core->cancelled)) return AGENT_ERR_CANCELLED;
    if(event->type==AGENT_EVENT_TEXT && event->length) {
        if(event->length>AGENT_ANSWER_MAX-e->total_text) return AGENT_ERR_LIMIT;
        e->total_text+=event->length; e->output_started=true;
    }
    return e->emit ? e->emit(e->emit_ctx,event) : AGENT_OK;
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
static bool trim_history(agent_engine_t *e,size_t *turn_start)
{
    if(*turn_start<=e->messages.system_used) return false;
    size_t removed=*turn_start-e->messages.system_used;
    memmove(e->messages.data+e->messages.system_used,e->messages.data+*turn_start,e->messages.used-*turn_start+1);
    e->messages.used-=removed; *turn_start=e->messages.system_used; return true;
}
static agent_err_t assistant(agent_engine_t *e,size_t *turn_start)
{
    agent_err_t error=agent_messages_assistant(&e->messages,&e->reply);
    if(error==AGENT_ERR_LIMIT && trim_history(e,turn_start)) error=agent_messages_assistant(&e->messages,&e->reply);
    return error;
}

/* A complete invalid batch has no side effects. Return every original call ID so
 * the model can correct it, without replaying a turn or losing protocol pairing. */
static agent_err_t repair_batch(agent_engine_t *e,size_t *turn_start)
{
    agent_err_t error=assistant(e,turn_start);
    for(unsigned i=0;!error && i<e->reply.call_count;++i) {
        char detail[384],result[640];
        agent_err_t check=agent_tool_check(e->reply.calls[i].name,agent_call_arguments(&e->reply,i),detail,sizeof(detail));
        if(!check) snprintf(detail,sizeof(detail),"Another call in this batch was invalid; nothing in this batch was executed. Submit the corrected batch.");
        agent_json_writer_t w;agent_json_writer_init(&w,result,sizeof(result));
        agent_json_printf(&w,"{\"executed\":false,\"error\":\"%s\",\"detail\":",check?agent_err_name(check):"batch_rejected");
        agent_json_quote(&w,detail);agent_json_raw(&w,"}");error=w.error;
        if(!error) {
            agent_event_t event={.type=AGENT_EVENT_TOOL_END,.data=result,.length=w.used,.index=i};
            error=emit(e,&event);
        }
        if(!error) {
            error=agent_messages_add(&e->messages,"tool",result,e->reply.calls[i].id);
            if(error==AGENT_ERR_LIMIT && trim_history(e,turn_start)) error=agent_messages_add(&e->messages,"tool",result,e->reply.calls[i].id);
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
    agent_json_writer_t w; agent_json_writer_init(&w,e->buffer,sizeof(e->buffer));
    agent_json_raw(&w,"{\"schema\":\"agent.turn/1\",\"device_id\":"); agent_json_quote(&w,e->context->device);
    agent_json_raw(&w,",\"user_id\":"); agent_json_quote(&w,e->context->user);
    agent_json_raw(&w,",\"session_id\":"); agent_json_quote(&w,e->context->session);
    agent_json_raw(&w,",\"turn_id\":"); agent_json_quote(&w,e->turn_id);
    agent_json_printf(&w,",\"stream\":%s,\"round\":%u,\"messages\":[",stream?"true":"false",round);
    /* Only this turn goes through the gateway; it owns cloud prompt assembly. */
    agent_json_raw(&w,e->messages.data+turn_start);
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

static agent_err_t write_messages(void *ctx,agent_write_fn write,void *write_ctx)
{
    agent_engine_t *e=ctx;
    if(!long_history(e)) return write(write_ctx,e->messages.data,e->messages.used);
    size_t boundary=e->messages.system_used-1;
    agent_err_t error=write(write_ctx,e->messages.data,boundary);
    if(!error) error=e->context_ops->replay(e->context,write,write_ctx);
    if(!error) error=write(write_ctx,e->messages.data+boundary,e->messages.used-boundary);
    return error;
}

typedef struct { agent_engine_t *engine; bool stream; } request_body_t;
static agent_err_t write_request(void *ctx,agent_write_fn write,void *write_ctx)
{
    const request_body_t *body=ctx;
    return body->engine->llm->compose(body->stream,write_messages,body->engine,agent_tools_write,NULL,write,write_ctx);
}

static agent_err_t complete(agent_engine_t *e,bool stream,size_t turn_start)
{
    agent_err_t error=AGENT_OK;
    bool streaming=long_history(e);
    request_body_t body={e,stream};
    for(unsigned attempt=0;attempt<2;++attempt) {
        error=AGENT_OK;
        size_t length=0;
        if(streaming) {
            error=e->context_ops->select(e->context,e->history_before,&e->core->cancelled);
            if(error) return error;
        }
        if(!error) error=e->gateway?gateway_request(e,stream,e->core->tool_rounds,turn_start,&length):
            agent_http_measure_body(write_request,&body,&e->core->cancelled,&length);
        if(error) { if(streaming) e->context_ops->release(e->context); return error; }
        e->context->request_bytes=length;
        agent_llm_reply_init(&e->reply); agent_sse_init(&e->sse,e->buffer,sizeof(e->buffer),&e->reply,emit,e);
        agent_http_request_t request={.target=e->gateway?AGENT_HTTP_GATEWAY:AGENT_HTTP_DEEPSEEK,
            .path=e->gateway?"/v1/agent/turns":"/chat/completions",.method="POST",.body=e->gateway?e->buffer:NULL,
            .length=length,.cancelled=&e->core->cancelled,.produce=e->gateway?NULL:write_request,.body_ctx=&body};
        e->used=0;
        error=e->transport->perform(e->transport->ctx,&request,stream?agent_sse_feed:feed,stream?(void *)&e->sse:(void *)e);
        if(streaming) e->context_ops->release(e->context);
        if(!error) error=stream?agent_sse_finish(&e->sse):e->llm->parse(&e->reply,e->buffer,e->used,false,emit,e);
        if(!error || !transient(error) || e->output_started || e->effects || attempt || !e->pause_ms) break;
        unsigned delay=500+(e->random_u32?e->random_u32()%500:0);
        for(unsigned waited=0;waited<delay;waited+=25) {
            if(atomic_load(&e->core->cancelled)) return AGENT_ERR_CANCELLED;
            e->pause_ms(25);
        }
    }
    return error;
}
agent_err_t agent_engine_turn(agent_engine_t *e,const char *input,bool stream)
{
    if(!input || !*input || strlen(input)>AGENT_INPUT_MAX || !agent_utf8_valid(input,strlen(input))) return AGENT_ERR_LIMIT;
    if(!e->core || !e->transport || !e->llm || !e->tools || !e->context || !e->context->wal.ready) return AGENT_ERR_CONFIG;
    if(!e->gateway && !e->llm->compose) return AGENT_ERR_CONFIG;
    if(!e->context_ops) e->context_ops=&agent_context_ops;
    uint64_t now=e->tools->now_ms(e->tools->ctx);
    if(now<e->circuit_until) return AGENT_ERR_BUSY;
    e->output_started=false; e->effects=false; e->total_text=0;
    agent_json_writer_t w; agent_json_writer_init(&w,e->messages.data,sizeof(e->messages.data));
    agent_json_raw(&w,"{\"format\":\"text/plain\",\"text\":"); agent_json_quote(&w,input); agent_json_raw(&w,"}");
    agent_err_t error=w.error;
    if(!error) error=e->context_ops->append(e->context,"message","user",e->messages.data,e->turn_id,sizeof(e->turn_id));
    if(error) return error;
    e->history_before=e->context->wal.next_seq;
    agent_err_t (*history)(agent_context_t *,agent_messages_t *,const char *)=
        long_history(e)?e->context_ops->prompt:e->context_ops->history;
    const char *base_prompt=
        "You are a small ESP-Hi embedded agent. Reply concisely in the user's language. Only fresh tool results and the current-turn snapshot are live device facts. "
        "Historical tool results may precede a reboot; never assume hardware is still on. Execute requested hardware actions and verify current status. "
        "If a tool returns executed=false, correct its arguments and try again within this turn. Never claim success from a rejected call. "
        "Call at most four tools in one response. Search context for earlier details missing from the visible history. "
        "Use durable summaries for known prior decisions and preferences; do not invent missing memories. "
        "Summaries can be stale: newer user statements override older summaries or assistant guesses."
        " Discover GPIO capabilities first. Use device_gpio_set for persistent input/output/PWM, "
        "device_gpio_get for readback, and a control plan for timed multi-step interactions. "
        "An accepted job is not finished; inspect control status. Plans restore their outputs when finished. "
        "Release a direct microphone or playback operation before a plan needing that resource."
#if AGENT_ENABLE_AUDIO
        " For requested music use device_audio_play_song: four expressive voices and up to 75 seconds. "
        "At 128 BPM, 16 two-bar sequence rows are one minute. Reuse patterns and include phrasing, dynamics and a clear ending. "
        "For a one-minute request use this reliable six-pattern layout: patterns 0 and 1 are lead variants with exactly eight notes each, ALL ticks=4; "
        "patterns 2 and 3 are bass variants with exactly two notes each, ALL ticks=16; pattern 4 is arpeggio with exactly eight notes, ALL ticks=4; "
        "pattern 5 is drums with exactly eight notes, ALL ticks=4. Choose pitches, velocities and gates creatively. "
        "Arrange 16 sequence rows from those six patterns, vary entrances and gain, and end on the tonic. TWO bars are 32 ticks: eight notes of 4 ticks, not sixteen. "
        "Assign separate lead/bass/arp/drum patterns; sequence references are zero-based and must exist. Check every pattern's tick sum before calling. "
        "For a continuous backing, fill all 32 ticks: two bass notes of 16 ticks or eight arpeggio/drum notes of 4 ticks. "
        "Keep lead, bass and arpeggio harmonically consistent and use a tonic bass at the ending. "
        "The older play_score is only a short monophonic cue. Keep song arguments compact and respond briefly after admission. "
        "Use audio status when checking playback completion. Microphone levels do not transcribe speech. "
        "Only mic_valid=true indicates a fresh level. Requested local capture/replay is available when clip_storage=true; "
        "use a light cue before capture in a plan. There is no speech recognition or audio upload."
#endif
    ;
    /* Context copies the system text into messages before reusing its scratch.
     * Reuse the idle HTTP buffer, not another resident prompt allocation. */
    int prompt_size=snprintf(e->buffer,sizeof(e->buffer),"%s\n%s",base_prompt,
        e->tools->hardware_prompt?e->tools->hardware_prompt:"");
    if(prompt_size>=0 && (size_t)prompt_size+768<sizeof(e->buffer) && e->tools->display && e->tools->display->status) {
        size_t n=(size_t)prompt_size;
        int added=snprintf(e->buffer+n,sizeof(e->buffer)-n,"\nCurrent-turn display snapshot (device uptime %llu ms): ",(unsigned long long)now);
        if(added>0 && (size_t)added<sizeof(e->buffer)-n) {
            n+=(size_t)added;
            agent_err_t state=e->tools->display->status(e->tools->display->ctx,e->buffer+n,sizeof(e->buffer)-n);
            if(state) snprintf(e->buffer+n,sizeof(e->buffer)-n,"unavailable: %s. Query before claiming any current state.",agent_err_name(state));
            prompt_size=(int)strlen(e->buffer);
        }
    }
    if(prompt_size<0 || (size_t)prompt_size>=sizeof(e->buffer)) error=AGENT_ERR_LIMIT;
    else error=history(e->context,&e->messages,e->buffer);
    size_t turn_start=e->messages.used;
    if(!error) error=agent_messages_add(&e->messages,"user",input,NULL);
    unsigned repairs=0;
    while(!error) {
        error=complete(e,stream,turn_start);
        if(error) break;
        if(!e->reply.call_count) { error=assistant(e,&turn_start); break; }
        error=agent_next_round(e->core);
        if(error) break;
        error=agent_tools_validate(&e->reply);
        if(error && repairs<2 && !e->effects && repairable_batch(&e->reply) &&
           (error==AGENT_ERR_ARGUMENT || error==AGENT_ERR_JSON || error==AGENT_ERR_LIMIT || error==AGENT_ERR_DUPLICATE)) {
            ++repairs;error=repair_batch(e,&turn_start);
            if(!error) continue;
        }
        if(!error) error=assistant(e,&turn_start);
        for(unsigned i=0;!error && i<e->reply.call_count;++i) {
            char result[2048],content[2560];
            if(atomic_load(&e->core->cancelled)) { error=AGENT_ERR_CANCELLED; break; }
            e->effects=true;
            error=agent_tool_invoke(e->tools,e->reply.calls[i].name,agent_call_arguments(&e->reply,i),result,sizeof(result));
            if(error) break;
            agent_event_t event={.type=AGENT_EVENT_TOOL_END,.data=result,.length=strlen(result),.index=i};
            error=emit(e,&event);
            agent_json_writer_init(&w,content,sizeof(content));
            agent_json_raw(&w,"{\"tool\":");
            const agent_tool_description_t *tool=agent_tool_find(e->reply.calls[i].name);
            agent_json_quote(&w,tool->name);
            agent_json_raw(&w,",\"call_id\":"); agent_json_quote(&w,e->reply.calls[i].id);
            agent_json_raw(&w,",\"turn_id\":"); agent_json_quote(&w,e->turn_id);
            agent_json_raw(&w,",\"result\":"); agent_json_raw(&w,result); agent_json_raw(&w,"}");
            if(!error) error=w.error;
            if(!error) error=e->context_ops->append(e->context,"tool_result","tool",content,NULL,0);
            if(!error) {
                error=agent_messages_add(&e->messages,"tool",result,e->reply.calls[i].id);
                if(error==AGENT_ERR_LIMIT && trim_history(e,&turn_start)) error=agent_messages_add(&e->messages,"tool",result,e->reply.calls[i].id);
            }
        }
    }
    if(!error) {
        size_t n=strlen(e->messages.data+turn_start);
        if(n+15>=sizeof(e->messages.data)) error=AGENT_ERR_LIMIT;
        else {
            memmove(e->messages.data+13,e->messages.data+turn_start,n);
            memcpy(e->messages.data,"{\"messages\":[",13);
            e->messages.data[13+n]='}'; e->messages.data[14+n]=0;
            error=e->context_ops->append(e->context,"turn","assistant",e->messages.data,NULL,0);
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
