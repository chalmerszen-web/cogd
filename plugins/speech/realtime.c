#include "realtime.h"
#include "pcm_json.h"
#include "crc.h"
#include <string.h>

/* ESP-HI uses explicit full-input commits. Portable builds may retain
 * automatic segmented input; disabled modes reject before opening TLS. */
#define RT_AUTOMATIC(q) (AGENT_RT_AUTOVAD && (q)->auto_vad)
#define RT_CONTINUOUS(q) (AGENT_RT_AUTOVAD && (q)->continuous)
#if AGENT_RT_TEXT_REUSE
#define RT_TEXT_REUSE(q) ((q)->text_reuse)
#else
#define RT_TEXT_REUSE(q) false
#endif

enum { TOP_KEY,TOP_COLON,TOP_VALUE,TOP_AFTER };
enum { FIELD_OTHER,FIELD_DELTA };
static uint64_t now(agent_realtime_t *q) { return q->speech->now_ms(q->speech->clock_ctx); }
static uint64_t deadline(agent_realtime_t *q)
{ return q->deadline_cap && q->deadline_cap<q->deadline?q->deadline_cap:q->deadline; }
static agent_err_t fail(agent_realtime_t *q,agent_err_t e)
{
    if(e && !q->error)q->error=e;
    if(e && q->opened) {q->ws->close(q->ws->ctx);q->opened=false;}
    return q->error;
}
static agent_err_t guard(agent_realtime_t *q)
{
    if(q->error)return q->error;
    if(q->speech->cancelled && atomic_load(q->speech->cancelled))return AGENT_ERR_CANCELLED;
    return now(q)>=deadline(q)?AGENT_ERR_TIMEOUT:AGENT_OK;
}
void agent_realtime_init(agent_realtime_t *q,agent_speech_t *s,const agent_ws_ops_t *ws,
                         const agent_pcm_sink_t *sink,agent_realtime_event_fn callback,void *ctx)
{
    memset(q,0,sizeof(*q));q->speech=s;q->ws=ws;q->on_event=callback;q->event_ctx=ctx;
    if(sink)q->sink=*sink;
}
void agent_realtime_cancel(agent_realtime_t *q)
{ if(q) {fail(q,AGENT_ERR_CANCELLED);q->done=false;} }
static agent_err_t append(agent_realtime_t *q,unsigned char ch)
{
    if(q->used+1>=AGENT_RT_EVENT_MAX)return AGENT_ERR_LIMIT;
    q->speech->scratch[q->used++]=(char)ch;q->speech->scratch[q->used]=0;return AGENT_OK;
}
static agent_err_t pcm_flush(agent_realtime_t *q,int16_t *block,size_t *used)
{
    if(!*used)return AGENT_OK;
    agent_err_t e=guard(q);if(e)return e;
    if(!q->pcm_open) {
        if(!q->sink.open || !q->sink.write)return AGENT_ERR_CONFIG;
        e=q->sink.open(q->sink.ctx,AGENT_RT_OUTPUT_RATE);if(e)return e;q->pcm_open=true;
    }
    e=q->sink.write(q->sink.ctx,block,*used);*used=0;return e?e:guard(q);
}
static agent_err_t pcm_byte(agent_realtime_t *q,unsigned byte,int16_t *block,size_t *used)
{
    if(q->pcm_bytes>=AGENT_RT_PCM_MAX || q->event_bytes>=AGENT_RT_OUTPUT_RATE*2u*4u)return AGENT_ERR_LIMIT;
    ++q->pcm_bytes;++q->event_bytes;
    if(!q->odd) {q->low_byte=(unsigned char)byte;q->odd=true;return AGENT_OK;}
    block[(*used)++]=(int16_t)((uint16_t)q->low_byte|((uint16_t)byte<<8));q->odd=false;
    return *used==128?pcm_flush(q,block,used):AGENT_OK;
}
static int b64_value(unsigned c)
{
    if(c>='A' && c<='Z')return (int)(c-'A');
    if(c>='a' && c<='z')return (int)(c-'a'+26);
    if(c>='0' && c<='9')return (int)(c-'0'+52);
    return c=='+'?62:c=='/'?63:c=='='?64:-1;
}
static agent_err_t base64_byte(agent_realtime_t *q,unsigned ch,int16_t *block,size_t *used)
{
    int v=b64_value(ch);if(v<0 || q->padded)return AGENT_ERR_PROTOCOL;
    q->b64[q->b64_used++]=(unsigned char)v;
    if(q->b64_used<4)return AGENT_OK;
    unsigned a=q->b64[0],b=q->b64[1],c=q->b64[2],d=q->b64[3];q->b64_used=0;
    if(a==64 || b==64 || (c==64 && (d!=64 || (b&15))) || (d==64 && c!=64 && (c&3)))return AGENT_ERR_PROTOCOL;
    agent_err_t e=pcm_byte(q,(a<<2)|(b>>4),block,used);
    if(!e && c!=64)e=pcm_byte(q,((b&15)<<4)|(c>>2),block,used);
    if(!e && d!=64)e=pcm_byte(q,((c&3)<<6)|d,block,used);
    q->padded=d==64;return e;
}
static agent_err_t audio_char(agent_realtime_t *q,unsigned ch,int16_t *block,size_t *used)
{
    if(q->unicode_digits) {
        unsigned digit=ch>='0' && ch<='9'?ch-'0':ch>='a' && ch<='f'?ch-'a'+10:ch>='A' && ch<='F'?ch-'A'+10:16;
        if(digit==16)return AGENT_ERR_JSON;
        q->unicode_value=(q->unicode_value<<4)|digit;
        if(--q->unicode_digits)return AGENT_OK;
        return base64_byte(q,q->unicode_value,block,used);
    }
    if(q->escape) {
        q->escape=false;
        if(ch=='u') {q->unicode_digits=4;q->unicode_value=0;return AGENT_OK;}
        return ch=='/'?base64_byte(q,ch,block,used):AGENT_ERR_PROTOCOL;
    }
    if(ch=='\\') {q->escape=true;return AGENT_OK;}
    if(ch=='"') {
        if(q->b64_used)return AGENT_ERR_PROTOCOL;
        q->audio_delta=false;q->in_string=false;q->top=TOP_AFTER;
        return append(q,ch);
    }
    return base64_byte(q,ch,block,used);
}
static agent_err_t delta_begin(agent_realtime_t *q)
{
    if(q->delta_seen)return AGENT_ERR_PROTOCOL;
    q->delta_seen=true;
    /* Validate all preceding fields, including duplicate or escaped names,
     * before any PCM is delivered. Temporarily complete the open delta/root. */
    if(q->used+3>AGENT_RT_EVENT_MAX)return AGENT_ERR_LIMIT;
    memcpy(q->speech->scratch+q->used,"\"}",3);
    cJSON *prefix=agent_json_parse(q->speech->scratch,q->used+2);
    q->speech->scratch[q->used]=0;
    if(!prefix)return AGENT_ERR_JSON;
    const char *type=agent_json_string(prefix,"type");
    agent_err_t e=type?AGENT_OK:AGENT_ERR_PROTOCOL;
    bool audio=type && !strcmp(type,"response.audio.delta");
    const char *id=agent_json_string(prefix,"response_id");
    if(audio && (!q->ready || !q->active || q->tool_transition_pending || !id || strcmp(id,q->response_id)))e=AGENT_ERR_PROTOCOL;
    cJSON_Delete(prefix);
    if(!e && audio)q->audio_delta=true;
    return e;
}
static agent_err_t metadata_char(agent_realtime_t *q,unsigned char ch)
{
    agent_err_t e=append(q,ch);if(e)return e;
    if(q->in_string) {
        if(q->escape) {q->escape=false;return AGENT_OK;}
        if(ch=='\\') {q->escape=true;return AGENT_OK;}
        if(ch!='"')return AGENT_OK;
        q->in_string=false;
        if(q->depth==1) {
            if(q->string_key) {
                size_t n=q->used-q->string_start;
                cJSON *key=agent_json_parse(q->speech->scratch+q->string_start,n);
                if(!cJSON_IsString(key)) {cJSON_Delete(key);return AGENT_ERR_JSON;}
                q->field=!strcmp(key->valuestring,"delta")?FIELD_DELTA:FIELD_OTHER;
                cJSON_Delete(key);q->top=TOP_COLON;
            } else q->top=TOP_AFTER;
        }
        return AGENT_OK;
    }
    if(ch=='"') {
        q->in_string=true;q->string_start=q->used-1;q->string_key=q->depth==1 && q->top==TOP_KEY;
        if(q->depth==1 && q->top==TOP_VALUE && q->field==FIELD_DELTA)return delta_begin(q);
    } else if(ch=='{' || ch=='[') {
        if(!q->depth) {if(ch!='{')return AGENT_ERR_JSON;q->top=TOP_KEY;}
        else if(q->depth==1 && q->top==TOP_VALUE)q->top=TOP_AFTER;
        if(++q->depth>16)return AGENT_ERR_LIMIT;
    } else if(ch=='}' || ch==']') {
        if(!q->depth)return AGENT_ERR_JSON;
        --q->depth;
    } else if(q->depth==1) {
        if(ch==':')q->top=TOP_VALUE;
        else if(ch==',') {q->top=TOP_KEY;q->field=FIELD_OTHER;}
        else if(ch!=' ' && ch!='\t' && ch!='\r' && ch!='\n' && q->top==TOP_VALUE)q->top=TOP_AFTER;
    }
    return AGENT_OK;
}
static agent_err_t provider_error(const cJSON *error)
{
    const char *code=agent_json_string(error,"code");
    return code && (strstr(code,"invalid_api_key") || strstr(code,"Unauthorized"))?AGENT_ERR_AUTH:
        code && (strstr(code,"Forbidden") || strstr(code,"permission_denied"))?AGENT_ERR_FORBIDDEN:
        code && (strstr(code,"rate_limit") || strstr(code,"Throttl"))?AGENT_ERR_RATE:AGENT_ERR_SERVER;
}
static bool text_configuration(const agent_realtime_t *q,const cJSON *session)
{
    const char *model=agent_json_string(session,"model"),*voice=agent_json_string(session,"voice");
    const char *instructions=agent_json_string(session,"instructions");
    const cJSON *modes=cJSON_GetObjectItemCaseSensitive(session,"modalities");
    const cJSON *tools=cJSON_GetObjectItemCaseSensitive(session,"tools");
    const cJSON *turn=cJSON_GetObjectItemCaseSensitive(session,"turn_detection");
    const cJSON *a=cJSON_GetArrayItem(modes,0),*b=cJSON_GetArrayItem(modes,1);
    uint64_t tokens=0;
    return model && !strcmp(model,AGENT_RT_MODEL) && voice && !strcmp(voice,"Tina") &&
        instructions && !strcmp(instructions,q->text_instructions) &&
        cJSON_IsArray(modes) && cJSON_GetArraySize(modes)==2 &&
        cJSON_IsString(a) && !strcmp(a->valuestring,"text") &&
        cJSON_IsString(b) && !strcmp(b->valuestring,"audio") &&
        (!turn || cJSON_IsNull(turn)) && (!tools || (cJSON_IsArray(tools) && !tools->child)) &&
        cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(session,"enable_search")) &&
        agent_json_uint(cJSON_GetObjectItemCaseSensitive(session,"max_tokens"),96,&tokens) && tokens==96;
}
static agent_err_t manual_input(agent_realtime_t *q,const cJSON *event,bool bind)
{
    const char *id=agent_json_string(event,"item_id");
    if(q->clearing || !q->input_started || !id || !*id || strlen(id)>=sizeof(q->input_item_id))return AGENT_ERR_PROTOCOL;
    if(q->input_item_id[0])return strcmp(id,q->input_item_id)?AGENT_ERR_PROTOCOL:AGENT_OK;
    if(q->input_count>=3)return AGENT_ERR_LIMIT;
    for(unsigned i=0;i<q->input_count;++i)
        if(!strcmp(id,q->inputs[i].id))return AGENT_ERR_PROTOCOL;
    /* A speculative preview can change identity before commit. It is never
     * the authority for final ASR; still reject identities from older turns. */
    if(!bind)return AGENT_OK;
    strcpy(q->input_item_id,id);strcpy(q->inputs[q->input_count++].id,id);
    return AGENT_OK;
}
#if defined(AGENT_RT_DIAGNOSTICS) && AGENT_RT_DIAGNOSTICS
static unsigned state_flags(const agent_realtime_t *q)
{
    return q->active | (q->pending<<1) | (q->done<<2) | (q->odd<<3) |
           (RT_CONTINUOUS(q)<<4) | (q->tool_transition_pending<<5) |
           (q->allow_tool_transition<<6) | (q->reusable<<7);
}
static unsigned identity_hash(const char *id)
{ return id?(unsigned)agent_crc32(id,strlen(id)):0; }
static void rejected_state(agent_realtime_t *q,const cJSON *event,unsigned before)
{
    if(!q->speech->event)return;
    const cJSON *response=cJSON_GetObjectItemCaseSensitive(event,"response");
    const char *id=agent_json_string(response,"id"),*top=agent_json_string(event,"response_id");
    char detail[256];agent_json_writer_t w;agent_json_writer_init(&w,detail,sizeof(detail));
    agent_json_printf(&w,"{\"before\":%u,\"after\":%u,\"top\":%u,\"id\":%u,\"current\":%u,\"segments\":%u,\"revision\":%u,\"pending_revision\":%u,\"pcm_bytes\":%u}",
        before,state_flags(q),identity_hash(top),identity_hash(id),identity_hash(q->response_id),
        q->input_count,q->response_revision,q->pending_revision,(unsigned)q->pcm_bytes);
    if(!w.error)q->speech->event(q->speech->event_ctx,"realtime_state",detail);
}
#endif
static agent_err_t complete_event(agent_realtime_t *q)
{
    cJSON *event=agent_json_parse(q->speech->scratch,q->used);
    if(!event)return AGENT_ERR_JSON;
    const char *type=agent_json_string(event,"type");agent_err_t e=AGENT_OK;
#if defined(AGENT_RT_DIAGNOSTICS) && AGENT_RT_DIAGNOSTICS
    unsigned before=state_flags(q);
#endif
    bool audio_end=type && (!strcmp(type,"response.audio.done") || !strcmp(type,"response.audio_transcript.done"));
    /* VAD cancellation can emit these bookkeeping markers without response_id
     * and even before response.created. Neither carries PCM or authority.
     * They never advance state: matching response.done still validates status,
     * identity, PCM parity and the full result. Audio/text deltas stay strict. */
    if(RT_CONTINUOUS(q) && audio_end) {
        cJSON_Delete(event);return AGENT_OK;
    }
    if(!type)e=AGENT_ERR_PROTOCOL;
    else if(!strcmp(type,"error") || !strcmp(type,"conversation.item.input_audio_transcription.failed"))
        e=provider_error(cJSON_GetObjectItemCaseSensitive(event,"error"));
    else if(!strcmp(type,"session.created")) {
        if(q->created)e=AGENT_ERR_PROTOCOL;else q->created=true;
    } else if(!strcmp(type,"session.updated")) {
        const cJSON *format=cJSON_GetObjectItemCaseSensitive(event,"session");
        bool configuration=!q->text_instructions || text_configuration(q,format);
        format=cJSON_GetObjectItemCaseSensitive(format,"audio");
        format=cJSON_GetObjectItemCaseSensitive(format,"output");
        format=cJSON_GetObjectItemCaseSensitive(format,"format");
        const char *kind=agent_json_string(format,"type");uint64_t rate=0;
        if(!configuration || !q->created || (q->ready && !q->updating) || !kind || strcmp(kind,"pcm") ||
           !agent_json_uint(cJSON_GetObjectItemCaseSensitive(format,"sample_rate"),AGENT_RT_OUTPUT_RATE,&rate) ||
           rate!=AGENT_RT_OUTPUT_RATE)e=AGENT_ERR_PROTOCOL;
        else {q->ready=true;q->updating=false;}
    } else if(!q->ready)e=AGENT_ERR_PROTOCOL;
    else if((q->text_input || RT_TEXT_REUSE(q)) && (!strncmp(type,"input_audio_buffer.",sizeof("input_audio_buffer.")-1) ||
            !strncmp(type,"conversation.item.input_audio_transcription.",
                     sizeof("conversation.item.input_audio_transcription.")-1)))e=AGENT_ERR_PROTOCOL;
    else if((q->text_input || RT_TEXT_REUSE(q)) && !strcmp(type,"conversation.item.created")) {
        const cJSON *item=cJSON_GetObjectItemCaseSensitive(event,"item");
        const char *id=agent_json_string(item,"id"),*role=agent_json_string(item,"role");
        if(role && !strcmp(role,"user")) {
            const cJSON *content=cJSON_GetObjectItemCaseSensitive(item,"content");
            const cJSON *part=cJSON_GetArrayItem(content,0);
            const char *kind=agent_json_string(item,"type"),*part_type=agent_json_string(part,"type");
            const char *text=agent_json_string(part,"text"),*status=agent_json_string(item,"status");
            if(!q->text_input || q->text_ready || !id || !*id || strlen(id)>=sizeof(q->input_item_id) ||
               !status || strcmp(status,"completed") || !kind || strcmp(kind,"message") ||
               !cJSON_IsArray(content) || cJSON_GetArraySize(content)!=1 || !part_type ||
               strcmp(part_type,"input_text") || !text || strcmp(text,q->text_input))e=AGENT_ERR_PROTOCOL;
            else {
                if(RT_TEXT_REUSE(q)) {
                    if(q->input_count>=3)e=AGENT_ERR_LIMIT;
                    for(unsigned i=0;i<q->input_count;++i)
                        if(!strcmp(id,q->inputs[i].id))e=AGENT_ERR_PROTOCOL;
                    if(!e)strcpy(q->inputs[q->input_count++].id,id);
                }
                if(!e) {strcpy(q->input_item_id,id);q->text_ready=true;}
            }
        } else if(!role || strcmp(role,"assistant") || !q->text_ready || !q->active)e=AGENT_ERR_PROTOCOL;
    }
    else if(!strcmp(type,"input_audio_buffer.cleared")) {
        if(!q->clearing)e=AGENT_ERR_PROTOCOL;else q->clearing=false;
    } else if(q->reusable && !strncmp(type,"conversation.item.input_audio_transcription.",
                                   sizeof("conversation.item.input_audio_transcription.")-1)) {
        bool final=!strcmp(type,"conversation.item.input_audio_transcription.completed");
        e=q->manual_draft && final && !q->input_item_id[0]?AGENT_ERR_PROTOCOL:
            manual_input(q,event,!q->manual_draft);
        if(!e && final) {
            if(q->input_final)e=AGENT_ERR_PROTOCOL;else q->input_final=true;
        }
    }
    else if(!strcmp(type,"input_audio_buffer.speech_started")) {
        const char *id=agent_json_string(event,"item_id");uint64_t at=0;
        if(!RT_AUTOMATIC(q) || (q->input_started && (!RT_CONTINUOUS(q) || !q->input_ended)) || !q->input_samples || !id || !*id || strlen(id)>=sizeof(q->input_item_id) ||
           !agent_json_uint(cJSON_GetObjectItemCaseSensitive(event,"audio_start_ms"),10000,&at) ||
           (RT_CONTINUOUS(q) && at>q->input_samples/16))e=AGENT_ERR_PROTOCOL;
        else {
            if(RT_CONTINUOUS(q)) {
                if(q->input_count==AGENT_RT_SEGMENTS)e=AGENT_ERR_LIMIT;
                for(unsigned i=0;i<q->input_count;++i)
                    if(!strcmp(id,q->inputs[i].id) || at<q->inputs[i].end_ms)e=AGENT_ERR_PROTOCOL;
                if(!e) {
                    agent_realtime_input_t *input=&q->inputs[q->input_count++];
                    strcpy(input->id,id);input->start_ms=(unsigned)at;q->input_ended=false;
                }
            }
            if(!e) {strcpy(q->input_item_id,id);q->input_started=true;q->input_start_ms=(unsigned)at;}
        }
    } else if(!strcmp(type,"input_audio_buffer.speech_stopped")) {
        const char *id=agent_json_string(event,"item_id");uint64_t at=0;
        if(!RT_AUTOMATIC(q) || !q->input_started || q->input_ended || !id || strcmp(id,q->input_item_id) ||
           !agent_json_uint(cJSON_GetObjectItemCaseSensitive(event,"audio_end_ms"),10000,&at) || at<q->input_start_ms ||
           cJSON_GetObjectItemCaseSensitive(event,"reason") ||
           (RT_CONTINUOUS(q) && at>q->input_samples/16))e=AGENT_ERR_PROTOCOL;
        else {
            q->input_end_ms=(unsigned)at;q->input_ended=true;
            if(RT_CONTINUOUS(q)) {
                agent_realtime_input_t *input=&q->inputs[q->input_count-1];
                input->end_ms=(unsigned)at;input->ended=true;
            } else q->input_paused=true;
        }
    } else if(RT_AUTOMATIC(q) && !RT_CONTINUOUS(q) && !strncmp(type,"response.",9) && (!q->input_ended || q->input_paused))e=AGENT_ERR_PROTOCOL;
    else if(!strcmp(type,"response.created")) {
        const char *id=agent_json_string(cJSON_GetObjectItemCaseSensitive(event,"response"),"id");
        bool transition=q->active && !q->pending && q->allow_tool_transition && !q->odd;
        if((q->text_input && !q->text_ready) || !id || !*id || strlen(id)>=sizeof(q->response_id) ||
           (q->active && (!transition || !strcmp(id,q->response_id))) || (!q->active && !q->pending))e=AGENT_ERR_PROTOCOL;
        else {
            if(RT_CONTINUOUS(q) || q->reusable || q->manual_draft || RT_TEXT_REUSE(q)) {
                /* Hash collisions only reject a session. A reused identity
                 * must never make an old packet look like the newest draft. */
                uint32_t hash=agent_crc32(id,strlen(id));
                if(q->response_count==AGENT_RT_SEGMENTS*2)e=AGENT_ERR_LIMIT;
                for(unsigned i=0;i<q->response_count;++i)
                    if(q->response_hashes[i]==hash)e=AGENT_ERR_PROTOCOL;
                if(!e)q->response_hashes[q->response_count++]=hash;
            }
            if(transition) {q->allow_tool_transition=false;q->tool_transition_pending=true;}
            else if(RT_CONTINUOUS(q)) {
                if(!q->pending_revision)e=AGENT_ERR_PROTOCOL;
                q->response_revision=q->pending_revision;q->pending_revision=0;
            }
            strcpy(q->response_id,id);q->active=true;q->pending=false;q->done=false;
            q->response_cancelled=false;
            q->pcm_open=false;q->pcm_bytes=0;q->odd=false;q->deadline=now(q)+45000;
        }
    } else if(q->tool_transition_pending && !strncmp(type,"response.",9)) {
        const cJSON *item=cJSON_GetObjectItemCaseSensitive(event,"item");
        const char *kind=agent_json_string(item,"type");uint64_t index=1;
        if(strcmp(type,"response.output_item.added") || !kind || strcmp(kind,"function_call") ||
           !agent_json_uint(cJSON_GetObjectItemCaseSensitive(event,"output_index"),0,&index))e=AGENT_ERR_PROTOCOL;
        else q->tool_transition_pending=false;
    } else if(!strcmp(type,"response.audio.delta")) {
        if(!q->active || !q->delta_seen || !agent_json_string(event,"delta"))e=AGENT_ERR_PROTOCOL;
    } else if(!strcmp(type,"response.audio.done")) {
        if(!q->active || q->odd)e=AGENT_ERR_PROTOCOL;
    } else if(!strcmp(type,"response.done")) {
        const cJSON *response=cJSON_GetObjectItemCaseSensitive(event,"response");
        const char *status=agent_json_string(response,"status"),*id=agent_json_string(response,"id");
        if(!q->active || q->odd || !status || !id || strcmp(id,q->response_id))e=AGENT_ERR_PROTOCOL;
        else if(strcmp(status,"completed") && (!RT_CONTINUOUS(q) || strcmp(status,"cancelled")))e=AGENT_ERR_SERVER;
        else {
            q->response_cancelled=!strcmp(status,"cancelled");
            q->active=false;q->done=true;q->deadline=now(q)+120000;
        }
    } else if(!strcmp(type,"input_audio_buffer.committed") && RT_AUTOMATIC(q)) {
        const char *id=agent_json_string(event,"item_id");
        if(!q->input_ended || q->input_paused || !id || strcmp(id,q->input_item_id))e=AGENT_ERR_PROTOCOL;
        else if(RT_CONTINUOUS(q)) {
            agent_realtime_input_t *input=&q->inputs[q->input_count-1];
            if(input->committed || q->pending_revision)e=AGENT_ERR_PROTOCOL;
            else {input->committed=true;q->pending_revision=q->input_count;q->pending=true;}
        }
    } else if(!strcmp(type,"input_audio_buffer.committed") && q->reusable) {
        e=q->input_ended?manual_input(q,event,true):AGENT_ERR_PROTOCOL;
    }
    const char *id=agent_json_string(event,"response_id");
    if(!e && id && (!q->active || strcmp(id,q->response_id)))e=AGENT_ERR_PROTOCOL;
    /* Error events are delivered too, so the caller can retain provider codes.
     * A failure remains latched even when the observer accepts the event. */
    if(q->on_event && (!e || (type && (!strcmp(type,"error") || !strcmp(type,"response.done") ||
                                     !strcmp(type,"conversation.item.input_audio_transcription.failed"))))) {
        if(e)q->error=e;
        q->dispatching=true;agent_err_t observed=q->on_event(q->event_ctx,event);q->dispatching=false;
        if(!e)e=observed;
    }
    if(e && q->speech->event) {
#if defined(AGENT_RT_DIAGNOSTICS) && AGENT_RT_DIAGNOSTICS
        rejected_state(q,event,before);
#endif
        q->speech->event(q->speech->event_ctx,"realtime_rejected",type && strlen(type)<=64?type:"invalid_event_type");
    }
    cJSON_Delete(event);return e;
}
agent_err_t agent_realtime_receive(agent_realtime_t *q,const char *data,const agent_ws_chunk_t *part)
{
    if(!q || !part || (!data && part->length))return AGENT_ERR_ARGUMENT;
    if(q->dispatching)return AGENT_ERR_BUSY;
    agent_err_t e=guard(q);if(e)return fail(q,e);
    if(q->input_paused)return AGENT_ERR_BUSY;
    if(part->first && part->opcode) {
        if(q->message || part->opcode!=1)return fail(q,AGENT_ERR_PROTOCOL);
        q->message=true;q->used=0;q->depth=0;q->top=TOP_KEY;q->field=FIELD_OTHER;
        q->in_string=q->escape=q->audio_delta=q->delta_seen=q->padded=false;
        q->b64_used=q->unicode_digits=0;q->event_bytes=0;
    } else if(!q->message || (part->first && part->opcode!=0))return fail(q,AGENT_ERR_PROTOCOL);
    int16_t block[128];size_t used=0;
    for(size_t i=0;i<part->length && !e;++i)
        e=q->audio_delta?audio_char(q,(unsigned char)data[i],block,&used):metadata_char(q,(unsigned char)data[i]);
    if(!e)e=pcm_flush(q,block,&used);
    if(!e && part->final) {
        q->message=false;
        if(q->audio_delta || q->in_string || q->depth)e=AGENT_ERR_JSON;
        else e=complete_event(q);
    }
    if(!e)e=guard(q);
    return e?fail(q,e):AGENT_OK;
}
agent_err_t agent_realtime_poll(agent_realtime_t *q,unsigned timeout_ms)
{
    if(!q || !q->opened)return q && q->error?q->error:AGENT_ERR_CONFIG;
    if(q->dispatching)return AGENT_ERR_BUSY;
    agent_err_t e=guard(q);if(e)return fail(q,e);
    if(q->input_paused)return AGENT_OK;
    uint64_t time=now(q),until=deadline(q);if(time>=until)return fail(q,AGENT_ERR_TIMEOUT);
    unsigned remaining=(unsigned)(until-time);if(timeout_ms>remaining)timeout_ms=remaining;
    char bytes[1024];agent_ws_chunk_t part={0};
    e=q->ws->read(q->ws->ctx,bytes,sizeof(bytes),&part,timeout_ms,q->speech->cancelled);
    if(!e && part.length>sizeof(bytes))e=AGENT_ERR_LIMIT;
    if(!e && (part.length || part.first || part.final))e=agent_realtime_receive(q,bytes,&part);
    if(!e)e=guard(q);
    return e?fail(q,e):AGENT_OK;
}
static void writer(agent_realtime_t *q,agent_json_writer_t *w,const char *type)
{
    agent_json_writer_init(w,q->speech->scratch+AGENT_RT_EVENT_MAX,AGENT_RT_SCRATCH-AGENT_RT_EVENT_MAX);
    agent_json_raw(w,"{\"event_id\":\"c");agent_json_printf(w,"%lu",(unsigned long)++q->speech->nonce);
    agent_json_raw(w,"\",\"type\":");agent_json_quote(w,type);
}
static agent_err_t send_json(agent_realtime_t *q,agent_json_writer_t *w,bool input)
{
    if(w->error)return fail(q,w->error);
    for(;;) {
        agent_err_t e=guard(q);if(e)return fail(q,e);
        if(q->input_paused || (input && q->input_ended && !RT_CONTINUOUS(q)))return AGENT_ERR_BUSY;
        e=q->ws->send(q->ws->ctx,false,w->data,w->used,q->speech->cancelled);
        if(e!=AGENT_ERR_BUSY)return e?fail(q,e):AGENT_OK;
        e=agent_realtime_poll(q,20);if(e)return e; /* BUSY means zero frame bytes sent. */
    }
}
static agent_err_t usable(agent_realtime_t *q)
{
    if(!q)return AGENT_ERR_ARGUMENT;
    if(q->dispatching)return AGENT_ERR_BUSY;
    if(q->error)return q->error;
    if(!q->opened || !q->ready)return AGENT_ERR_CONFIG;
    agent_err_t e=guard(q);return e?fail(q,e):AGENT_OK;
}
agent_err_t agent_realtime_begin(agent_realtime_t *q,const char *instructions,const char *tools,bool auto_vad)
{
    if(!q || !q->speech || !q->ws || !q->ws->open || !q->ws->send || !q->ws->read || !q->ws->close ||
       !q->speech->scratch || q->speech->capacity<AGENT_RT_SCRATCH || !q->speech->now_ms)return AGENT_ERR_CONFIG;
    if(q->opened || q->dispatching || q->error)return q->error?q->error:AGENT_ERR_BUSY;
    if(!AGENT_RT_AUTOVAD && (auto_vad || q->continuous))return AGENT_ERR_ARGUMENT;
    if(!instructions || !tools || (RT_CONTINUOUS(q) && !auto_vad) || (q->reusable && auto_vad) ||
       (RT_TEXT_REUSE(q) && !q->text_instructions))return AGENT_ERR_ARGUMENT;
    if(q->text_instructions && (auto_vad || q->reusable || q->manual_draft || strcmp(tools,"[]")))return AGENT_ERR_ARGUMENT;
    cJSON *array=agent_json_parse(tools,strlen(tools));bool valid=cJSON_IsArray(array);cJSON_Delete(array);
    if(!valid)return AGENT_ERR_JSON;
    q->auto_vad=auto_vad;q->deadline=now(q)+10000;
    agent_json_writer_t w;writer(q,&w,"session.update");
    agent_json_raw(&w,",\"session\":{\"modalities\":[\"text\",\"audio\"],\"voice\":\"Tina\",\"input_audio_format\":\"pcm\",\"audio\":{\"output\":{\"format\":{\"type\":\"pcm\",\"sample_rate\":16000}}},\"enable_search\":false,\"turn_detection\":");
    agent_json_raw(&w,!auto_vad?"null":RT_CONTINUOUS(q)?
        "{\"type\":\"server_vad\",\"threshold\":0.1,\"silence_duration_ms\":200}":
        "{\"type\":\"server_vad\",\"threshold\":0.1,\"silence_duration_ms\":800}");
    agent_json_raw(&w,",\"instructions\":");agent_json_quote(&w,instructions);
    if(q->text_instructions)agent_json_raw(&w,",\"max_tokens\":96");
    agent_json_raw(&w,",\"tools\":");agent_json_raw(&w,tools);agent_json_raw(&w,"}}");
    if(w.error)return fail(q,w.error);
    agent_err_t e=guard(q);if(e)return fail(q,e);
    e=q->ws->open(q->ws->ctx,q->speech->cancelled);if(e)return fail(q,e);q->opened=true;
    while(!e && !q->created)e=agent_realtime_poll(q,50);
    if(!e)e=send_json(q,&w,false);
    while(!e && !q->ready)e=agent_realtime_poll(q,50);
    if(!e)q->deadline=now(q)+120000;
    return e;
}
agent_err_t agent_realtime_begin_text(agent_realtime_t *q,const char *instructions)
{
    if(!q || !instructions || !*instructions)return AGENT_ERR_ARGUMENT;
    if(q->opened || q->created || q->text_instructions)return AGENT_ERR_BUSY;
    q->text_instructions=instructions;
    return agent_realtime_begin(q,instructions,"[]",false);
}
#if AGENT_RT_TEXT_REUSE
agent_err_t agent_realtime_next_text_turn(agent_realtime_t *q)
{
    agent_err_t e=usable(q);if(e)return e;
    if(!q->text_reuse || !q->text_instructions || !q->text_input || !q->text_ready ||
       !q->done || !q->input_count || !q->response_count || q->active || q->pending ||
       q->updating || q->clearing || q->message || q->odd)return AGENT_ERR_PROTOCOL;
    if(q->input_count>=3)return AGENT_ERR_LIMIT;
    q->text_input=NULL;q->text_ready=q->done=false;q->input_item_id[0]=0;
    q->deadline=now(q)+120000;return AGENT_OK;
}
#endif
agent_err_t agent_realtime_feed_pcm_some(agent_realtime_t *q,const int16_t *pcm,size_t count,size_t *accepted)
{
    if(!accepted)return AGENT_ERR_ARGUMENT;
    *accepted=0;
    agent_err_t e=usable(q);if(e)return e;
    if(q->text_input || q->text_instructions)return AGENT_ERR_PROTOCOL;
    if(!pcm && count)return AGENT_ERR_ARGUMENT;
    if(q->input_ended && !RT_CONTINUOUS(q) && count)return AGENT_ERR_BUSY;
    if(count>AGENT_RT_INPUT_MAX-q->input_samples)return fail(q,AGENT_ERR_LIMIT);
    while(count) {
        size_t n=count>512?512:count;agent_json_writer_t w;writer(q,&w,"input_audio_buffer.append");
        agent_json_raw(&w,",\"audio\":\"");
        agent_pcm_json(&w,pcm,n);
        agent_json_raw(&w,"\"}");e=send_json(q,&w,true);if(e)return e;
        q->input_samples+=n;if(q->reusable)q->input_started=true;
        *accepted+=n;pcm+=n;count-=n;
        e=agent_realtime_poll(q,0);if(e)return e;
        /* Duplex audio can arrive faster than one1KiB read per32ms input.
         * Drain a bounded part of the current frame without blocking capture. */
        for(unsigned i=0;(RT_CONTINUOUS(q) || q->manual_draft) && q->message && i<4;++i) {
            e=agent_realtime_poll(q,0);if(e)return e;
        }
    }
    return AGENT_OK;
}
agent_err_t agent_realtime_feed_pcm(agent_realtime_t *q,const int16_t *pcm,size_t count)
{ size_t accepted;return agent_realtime_feed_pcm_some(q,pcm,count,&accepted); }
agent_err_t agent_realtime_resume(agent_realtime_t *q)
{
    agent_err_t e=usable(q);if(e)return e;
    if(RT_CONTINUOUS(q) || !RT_AUTOMATIC(q) || !q->input_ended || !q->input_paused || q->message || q->active || q->pending)return AGENT_ERR_PROTOCOL;
    q->input_paused=false;q->pending=true;q->done=false;q->deadline=now(q)+45000;
    return AGENT_OK;
}
agent_err_t agent_realtime_create_response(agent_realtime_t *q)
{
    agent_err_t e=usable(q);if(e)return e;
    if(RT_CONTINUOUS(q))return AGENT_ERR_PROTOCOL;
    if(q->active || q->pending || q->updating)return AGENT_ERR_BUSY;
    if(RT_TEXT_REUSE(q) && !q->text_input)return AGENT_ERR_PROTOCOL;
    /* A verified tool-free text session may queue create immediately after
     * its text item on the same ordered transport. The receive path still
     * requires the exact item acknowledgement BEFORE response.created/PCM.
     * Legacy callers without a validated text configuration keep the barrier. */
    if(q->text_input && !q->text_ready && !q->text_instructions)return AGENT_ERR_BUSY;
    if(q->text_input && q->done)return AGENT_ERR_PROTOCOL;
    if(RT_AUTOMATIC(q) && (!q->input_ended || q->input_paused || !q->done))return AGENT_ERR_PROTOCOL;
    agent_json_writer_t w;writer(q,&w,"response.create");agent_json_raw(&w,"}");
    q->pending=true;q->done=false;q->deadline=now(q)+45000;return send_json(q,&w,false);
}
agent_err_t agent_realtime_user_text(agent_realtime_t *q,const char *text)
{
    agent_err_t e=usable(q);if(e)return e;
    if(q->active || q->pending || q->updating || q->clearing)return AGENT_ERR_BUSY;
    if(RT_AUTOMATIC(q) || RT_CONTINUOUS(q) || q->reusable || q->manual_draft ||
       q->text_input || q->input_samples || q->input_started || q->done ||
       (q->response_id[0] && !RT_TEXT_REUSE(q)))return AGENT_ERR_PROTOCOL;
    if(RT_TEXT_REUSE(q) && q->input_count>=3)return AGENT_ERR_LIMIT;
    if(!text || !*text || strlen(text)>512 || !agent_utf8_valid(text,strlen(text)))return AGENT_ERR_ARGUMENT;
    agent_json_writer_t w;writer(q,&w,"conversation.item.create");
    agent_json_raw(&w,",\"item\":{\"type\":\"message\",\"role\":\"user\",\"content\":[{\"type\":\"input_text\",\"text\":");
    agent_json_quote(&w,text);agent_json_raw(&w,"}]}}");
    e=send_json(q,&w,false);
    if(!e)q->text_input=text;
    return e;
}
agent_err_t agent_realtime_update(agent_realtime_t *q,const char *instructions,const char *tools)
{
    agent_err_t e=usable(q);if(e)return e;
    if(q->text_input || q->text_instructions)return AGENT_ERR_PROTOCOL;
    if(RT_AUTOMATIC(q) || RT_CONTINUOUS(q) || !instructions || !tools)return AGENT_ERR_ARGUMENT;
    if(q->active || q->pending || q->updating || q->clearing)return AGENT_ERR_BUSY;
    cJSON *array=agent_json_parse(tools,strlen(tools));bool valid=cJSON_IsArray(array);cJSON_Delete(array);
    if(!valid)return AGENT_ERR_JSON;
    agent_json_writer_t w;writer(q,&w,"session.update");
    agent_json_raw(&w,",\"session\":{\"turn_detection\":null,\"instructions\":");
    agent_json_quote(&w,instructions);agent_json_raw(&w,",\"tools\":");
    agent_json_raw(&w,tools);agent_json_raw(&w,"}}");
    if(w.error)return fail(q,w.error);
    /* BUSY means no bytes sent, so it cannot authorize an update echo yet.
     * Input counters, ASR identity and parser state are deliberately retained. */
    e=send_json(q,&w,false);if(!e)q->updating=true;return e;
}
agent_err_t agent_realtime_commit_input(agent_realtime_t *q)
{
    agent_err_t e=usable(q);if(e)return e;
    if(RT_AUTOMATIC(q) || !q->input_samples)return AGENT_ERR_PROTOCOL;
    if((!q->manual_draft && (q->active || q->pending)) || q->updating)return AGENT_ERR_BUSY;
    agent_json_writer_t w;writer(q,&w,"input_audio_buffer.commit");agent_json_raw(&w,"}");
    e=send_json(q,&w,false);if(e)return e;q->input_samples=0;
    if(q->reusable)q->input_ended=true;
    return AGENT_OK;
}
agent_err_t agent_realtime_commit(agent_realtime_t *q)
{
    /* The combined API must not commit then fail its implicit create. */
    if(q && q->manual_draft && (q->active || q->pending))return AGENT_ERR_BUSY;
    agent_err_t e=agent_realtime_commit_input(q);
    return e?e:agent_realtime_create_response(q);
}
agent_err_t agent_realtime_next_turn(agent_realtime_t *q)
{
    agent_err_t e=usable(q);if(e)return e;
    if(!q->reusable || RT_AUTOMATIC(q) || q->input_count>=3 || !q->done || !q->input_final ||
       !q->input_ended || q->active || q->pending || q->updating || q->message || q->odd)return AGENT_ERR_PROTOCOL;
    q->deadline_cap=now(q)+1000;q->clearing=true;
    agent_json_writer_t w;writer(q,&w,"input_audio_buffer.clear");agent_json_raw(&w,"}");
    e=send_json(q,&w,false);
    while(!e && q->clearing)e=agent_realtime_poll(q,20);
    if(e)return e;
    q->done=q->input_started=q->input_ended=q->input_final=false;
    q->input_samples=0;q->input_item_id[0]=0;
    q->allow_tool_transition=true;q->tool_transition_pending=false;
    q->deadline_cap=0;q->deadline=now(q)+120000;
    return AGENT_OK;
}
agent_err_t agent_realtime_tool_result(agent_realtime_t *q,const char *id,const char *output)
{
    agent_err_t e=usable(q);if(e)return e;
    if(q->text_input || RT_TEXT_REUSE(q))return AGENT_ERR_PROTOCOL;
    if(RT_CONTINUOUS(q))return AGENT_ERR_PROTOCOL;
    if(q->active || q->pending || q->updating)return AGENT_ERR_BUSY;
    if(RT_AUTOMATIC(q) && (!q->input_ended || q->input_paused || !q->done))return AGENT_ERR_PROTOCOL;
    if(!id || !*id || strlen(id)>128 || !output)return AGENT_ERR_ARGUMENT;
    cJSON *json=agent_json_parse(output,strlen(output));if(!json)return AGENT_ERR_JSON;cJSON_Delete(json);
    agent_json_writer_t w;writer(q,&w,"conversation.item.create");
    agent_json_raw(&w,",\"item\":{\"type\":\"function_call_output\",\"call_id\":");agent_json_quote(&w,id);
    agent_json_raw(&w,",\"output\":");agent_json_quote(&w,output);agent_json_raw(&w,"}}");
    return send_json(q,&w,false);
}
