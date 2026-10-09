#include "asr_realtime.h"
#include "json.h"
#include "pcm_json.h"
#include <stdio.h>
#include <string.h>

static void close_session(agent_asr_realtime_t *q)
{
    if(q->opened)q->ws->close(q->ws->ctx);
    q->opened=false;
}
static agent_err_t fail(agent_asr_realtime_t *q,agent_err_t error)
{
    if(error) {
        if(!q->error)q->error=error;
        if(q->segments)q->segments->error=q->error;
        if(q->text && q->capacity)q->text[0]=0;
        close_session(q);
    }
    return error?q->error:AGENT_OK;
}
static agent_err_t guard(agent_asr_realtime_t *q)
{
    if(q->error)return q->error;
    if(q->speech->cancelled && atomic_load(q->speech->cancelled))return AGENT_ERR_CANCELLED;
    return q->speech->now_ms(q->speech->clock_ctx)>=q->deadline?AGENT_ERR_TIMEOUT:AGENT_OK;
}
static void event(agent_asr_realtime_t *q,const char *kind,const char *text)
{
    agent_speech_t *s=q->speech;
    if(s->event)s->event(s->event_ctx,kind,text);
}
static void preview(agent_asr_realtime_t *q,size_t confirmed,const char *kind)
{
    /* Synchronous borrowed views of one buffer; restore before the complete
     * preview callback. Equal previews can still advance this boundary. */
    char tail=q->text[confirmed];q->text[confirmed]=0;
    event(q,"asr_prefix",q->text);q->text[confirmed]=tail;
    event(q,kind,q->text);
}
void agent_asr_realtime_init(agent_asr_realtime_t *q,agent_speech_t *s,
    const agent_ws_ops_t *ws,char *text,size_t capacity)
{
    *q=(agent_asr_realtime_t){.speech=s,.ws=ws,.text=text,.capacity=capacity};
    if(text && capacity)text[0]=0;
}
agent_err_t agent_asr_realtime_use_vad(agent_asr_realtime_t *q,agent_asr_segments_t *segments)
{
    if(!q || !segments || !q->text || !q->capacity)return AGENT_ERR_ARGUMENT;
    if(q->opened || q->created || q->error || q->segments)return AGENT_ERR_BUSY;
    agent_asr_segments_init(segments,q->text,q->capacity);q->segments=segments;
    return AGENT_OK;
}
static bool equals(const cJSON *object,const char *key,const char *value)
{
    const char *s=agent_json_string(object,key);
    return s && !strcmp(s,value);
}
static bool identifier(const char *s) { return s && *s && strlen(s)<=64; }
static bool config(const cJSON *session,bool configured,bool server_vad)
{
    const cJSON *modes=cJSON_GetObjectItemCaseSensitive(session,"modalities");
    uint64_t rate=0;
    if(!equals(session,"model",AGENT_ASR_RT_MODEL) || !equals(session,"input_audio_format","pcm") ||
       !agent_json_uint(cJSON_GetObjectItemCaseSensitive(session,"sample_rate"),16000,&rate) || rate!=16000 ||
       !cJSON_IsArray(modes) || cJSON_GetArraySize(modes)!=1 ||
       !cJSON_IsString(cJSON_GetArrayItem(modes,0)) || strcmp(cJSON_GetArrayItem(modes,0)->valuestring,"text"))return false;
    const cJSON *vad=cJSON_GetObjectItemCaseSensitive(session,"turn_detection");
    if(!configured)return true;
    if(!server_vad)return !vad || cJSON_IsNull(vad);
    const cJSON *threshold=cJSON_GetObjectItemCaseSensitive(vad,"threshold");
    uint64_t silence;
    return equals(vad,"type","server_vad") && cJSON_IsNumber(threshold) && threshold->valuedouble==0.2 &&
        agent_json_uint(cJSON_GetObjectItemCaseSensitive(vad,"silence_duration_ms"),700,&silence) && silence==700;
}
static agent_err_t identity(agent_asr_realtime_t *q,const cJSON *root,bool bind)
{
    const cJSON *item=cJSON_GetObjectItemCaseSensitive(root,"item_id");
    if(!item)return AGENT_OK; /* Isolation, not a fabricated remote identity. */
    if(!cJSON_IsString(item) || !identifier(item->valuestring))return AGENT_ERR_PROTOCOL;
    if(q->input_id[0] && strcmp(q->input_id,item->valuestring))return AGENT_ERR_PROTOCOL;
    if(bind)strcpy(q->input_id,item->valuestring);
    return AGENT_OK;
}
static agent_err_t transcript(agent_asr_realtime_t *q,const cJSON *root,bool final)
{
    uint64_t index;
    if(!agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,"content_index"),0,&index))return AGENT_ERR_PROTOCOL;
    agent_err_t error=identity(q,root,false);if(error)return error;
    const char *text=agent_json_string(root,final?"transcript":"text");
    const char *stash=final?"":agent_json_string(root,"stash");
    if(!text || !stash || (final && !*text))return AGENT_ERR_PROTOCOL;
    size_t a=strlen(text),b=strlen(stash);
    if(a+b>AGENT_INPUT_MAX || a+b>=q->capacity)return AGENT_ERR_LIMIT;
    memcpy(q->text,text,a);memcpy(q->text+a,stash,b+1);
    if(final) {q->final=true;event(q,"asr_final_received",NULL);}
    else preview(q,a,"asr_partial");
    return AGENT_OK;
}
static agent_err_t dispatch(agent_asr_realtime_t *q,const cJSON *root)
{
    const char *type=agent_json_string(root,"type"),*id=agent_json_string(root,"event_id");
    if(!type || !identifier(id) || ++q->events>512)return AGENT_ERR_PROTOCOL;
    if(!strcmp(type,"error") || !strcmp(type,"conversation.item.input_audio_transcription.failed"))return AGENT_ERR_SERVER;
    if(!strcmp(type,"session.created")) {
        const cJSON *s=cJSON_GetObjectItemCaseSensitive(root,"session");
        id=agent_json_string(s,"id");
        if(q->created || !identifier(id) || !config(s,false,false))return AGENT_ERR_PROTOCOL;
        strcpy(q->session_id,id);q->created=true;return AGENT_OK;
    }
    if(!q->created)return AGENT_ERR_PROTOCOL;
    if(!strcmp(type,"session.updated")) {
        const cJSON *s=cJSON_GetObjectItemCaseSensitive(root,"session");
        if(!q->updating || q->ready || !equals(s,"id",q->session_id) || !config(s,true,q->segments!=NULL))return AGENT_ERR_PROTOCOL;
        q->ready=true;q->updating=false;return AGENT_OK;
    }
    if(!q->ready || q->done)return AGENT_ERR_PROTOCOL;
    if(!strcmp(type,"session.finished")) {
        if(q->segments && q->finishing && q->segments->used && agent_asr_segments_settled(q->segments)) {
            q->final=true;event(q,"asr_final_received",NULL);
        }
        if(!q->finishing || !q->final)return AGENT_ERR_PROTOCOL;
        q->done=true;return AGENT_OK;
    }
    if(q->final || !q->input_samples)return AGENT_ERR_PROTOCOL;
    if(q->segments) {
        agent_asr_segment_update_t update;
        agent_err_t error=agent_asr_segments_receive(q->segments,root,q->input_samples,&update);
        if(error)return error;
        if(update.notify) {
            if(q->speech->asr_sentence)q->speech->asr_sentence(q->speech->asr_sentence_ctx,
                update.revision,update.end_ms,update.settled,q->text[0]!=0,update.end_ms!=0);
            preview(q,update.confirmed_bytes,
                update.partial?"asr_partial":update.settled?"asr_segment_final":"asr_segment_pending");
        }
        return AGENT_OK;
    }
    if(!strcmp(type,"input_audio_buffer.committed")) {
        if(!q->committing || q->committed)return AGENT_ERR_PROTOCOL;
        agent_err_t error=identity(q,root,true);if(error)return error;
        q->committed=true;event(q,"asr_committed",NULL);return AGENT_OK;
    }
    if(!strcmp(type,"conversation.item.input_audio_transcription.completed")) {
        if(!q->committed)return AGENT_ERR_PROTOCOL;
        return transcript(q,root,true);
    }
    if(!strcmp(type,"conversation.item.input_audio_transcription.text"))return transcript(q,root,false);
    if(!strcmp(type,"conversation.item.created")) {
        const cJSON *item=cJSON_GetObjectItemCaseSensitive(root,"item");
        const cJSON *content=cJSON_GetObjectItemCaseSensitive(item,"content");
        /* Actual standalone-ASR advisory shape. Never bind an input here. */
        if(++q->announcements>3 || cJSON_GetObjectItemCaseSensitive(item,"id") ||
           !equals(item,"type","message") || !equals(item,"role","assistant") ||
           !equals(item,"status","in_progress") || !cJSON_IsArray(content) ||
           cJSON_GetArraySize(content)!=1 || !equals(cJSON_GetArrayItem(content,0),"type","input_audio"))return AGENT_ERR_PROTOCOL;
        const cJSON *text=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(content,0),"transcript");
        return text && !cJSON_IsNull(text)?AGENT_ERR_PROTOCOL:AGENT_OK;
    }
    return AGENT_ERR_PROTOCOL;
}
agent_err_t agent_asr_realtime_receive(agent_asr_realtime_t *q,const char *data,const agent_ws_chunk_t *part)
{
    agent_err_t error=guard(q);if(error)return fail(q,error);
    if(!q->opened || !part || (!data && part->length))return fail(q,AGENT_ERR_ARGUMENT);
    if(part->first && part->opcode) {
        if(q->message || part->opcode!=1)return fail(q,AGENT_ERR_PROTOCOL);
        q->message=true;q->used=0;
    } else if(!q->message)return fail(q,AGENT_ERR_PROTOCOL);
    if(part->length>=AGENT_ASR_RT_EVENT_MAX-q->used)return fail(q,AGENT_ERR_LIMIT);
    if(part->length)memcpy(q->speech->scratch+q->used,data,part->length);
    q->used+=part->length;
    if(part->final) {
        cJSON *root=agent_json_parse(q->speech->scratch,q->used);
        error=root?dispatch(q,root):AGENT_ERR_JSON;
        cJSON_Delete(root);q->message=false;q->used=0;
    }
    return fail(q,error);
}
agent_err_t agent_asr_realtime_poll(agent_asr_realtime_t *q,unsigned ms)
{
    agent_err_t error=guard(q);if(error)return fail(q,error);
    if(!q->opened)return fail(q,AGENT_ERR_PROTOCOL);
    char bytes[512];agent_ws_chunk_t part={0};
    error=q->ws->read(q->ws->ctx,bytes,sizeof(bytes),&part,ms>50?50:ms,q->speech->cancelled);
    if(!error && part.length>sizeof(bytes))error=AGENT_ERR_LIMIT;
    if(!error && (part.length || part.first))return agent_asr_realtime_receive(q,bytes,&part);
    return fail(q,error);
}
static void writer(agent_asr_realtime_t *q,agent_json_writer_t *w,const char *type)
{
    agent_json_writer_init(w,q->speech->scratch+AGENT_ASR_RT_EVENT_MAX,
                           AGENT_ASR_RT_SCRATCH-AGENT_ASR_RT_EVENT_MAX);
    agent_json_printf(w,"{\"event_id\":\"asr_%lu\",\"type\":",(unsigned long)++q->speech->nonce);
    agent_json_quote(w,type);
}
static agent_err_t send_json(agent_asr_realtime_t *q,agent_json_writer_t *w)
{
    if(w->error)return fail(q,w->error);
    for(;;) {
        agent_err_t error=guard(q);if(error)return fail(q,error);
        error=q->ws->send(q->ws->ctx,false,w->data,w->used,q->speech->cancelled);
        if(error!=AGENT_ERR_BUSY)return fail(q,error);
        /* BUSY guarantees no frame sent. RX occupies only the other region. */
        error=agent_asr_realtime_poll(q,20);if(error)return error;
    }
}
static agent_err_t wait_flag(agent_asr_realtime_t *q,const bool *flag,unsigned ms)
{
    uint64_t end=q->speech->now_ms(q->speech->clock_ctx)+ms;
    while(!*flag) {
        agent_err_t error=agent_asr_realtime_poll(q,20);if(error)return error;
        if(q->speech->now_ms(q->speech->clock_ctx)>=end)return fail(q,AGENT_ERR_TIMEOUT);
    }
    return fail(q,guard(q));
}
agent_err_t agent_asr_realtime_begin(agent_asr_realtime_t *q)
{
    if(!q || !q->speech || !q->ws || !q->ws->open || !q->ws->close || !q->ws->send || !q->ws->read ||
       !q->speech->scratch || q->speech->capacity<AGENT_ASR_RT_SCRATCH || !q->speech->now_ms ||
       !q->text || !q->capacity)return AGENT_ERR_CONFIG;
    if(q->opened || q->created || q->error)return AGENT_ERR_BUSY;
    q->text[0]=0;q->deadline=q->speech->now_ms(q->speech->clock_ctx)+35000;
    agent_err_t error=guard(q);if(error)return fail(q,error);
    event(q,"asr_connect",NULL);
    error=q->ws->open(q->ws->ctx,q->speech->cancelled);
    if(error)return fail(q,error);
    q->opened=true;
    event(q,"asr_connected",NULL);
    error=wait_flag(q,&q->created,5000);
    if(!error) {
        agent_json_writer_t w;writer(q,&w,"session.update");
        agent_json_raw(&w,",\"session\":{\"input_audio_format\":\"pcm\",\"sample_rate\":16000,\"turn_detection\":");
        agent_json_raw(&w,q->segments?"{\"type\":\"server_vad\",\"threshold\":0.2,\"silence_duration_ms\":700}":"null");
        agent_json_raw(&w,"}}");
        error=send_json(q,&w);q->updating=!error;
    }
    if(!error)error=wait_flag(q,&q->ready,5000);
    if(!error)event(q,"asr_started",NULL);
    return error;
}
agent_err_t agent_asr_realtime_feed_pcm(agent_asr_realtime_t *q,const int16_t *pcm,size_t count)
{
    if(!q->opened || !q->ready || q->committing || q->finishing)return fail(q,AGENT_ERR_PROTOCOL);
    if((!pcm && count) || count>AGENT_ASR_RT_INPUT_MAX-q->input_samples)return fail(q,AGENT_ERR_LIMIT);
    while(count) {
        size_t n=count>AGENT_ASR_RT_CHUNK?AGENT_ASR_RT_CHUNK:count;
        agent_json_writer_t w;writer(q,&w,"input_audio_buffer.append");agent_json_raw(&w,",\"audio\":\"");
        agent_pcm_json(&w,pcm,n);
        agent_json_raw(&w,"\"}");
        agent_err_t error=send_json(q,&w);if(error)return error;
        q->input_samples+=n;pcm+=n;count-=n;
        error=agent_asr_realtime_poll(q,0);if(error)return error;
    }
    return fail(q,guard(q));
}
agent_err_t agent_asr_realtime_finish(agent_asr_realtime_t *q)
{
    if(!q->opened || !q->ready || q->committing || q->finishing || !q->input_samples)return fail(q,AGENT_ERR_PROTOCOL);
    if(q->segments) {
        agent_json_writer_t w;writer(q,&w,"session.finish");agent_json_raw(&w,"}");
        event(q,"asr_finish",NULL);
        agent_err_t error=send_json(q,&w);q->finishing=!error;
        if(!error)error=wait_flag(q,&q->done,8000);
        close_session(q);
        if(!error)event(q,"asr_text",q->text);
        return error;
    }
    agent_json_writer_t w;writer(q,&w,"input_audio_buffer.commit");agent_json_raw(&w,"}");
    event(q,"asr_finish",NULL);
    agent_err_t error=send_json(q,&w);q->committing=!error;
    if(!error)error=wait_flag(q,&q->final,8000);
    if(!error) {
        writer(q,&w,"session.finish");agent_json_raw(&w,"}");
        error=send_json(q,&w);q->finishing=!error;
    }
    if(!error)error=wait_flag(q,&q->done,5000);
    close_session(q);
    if(!error)event(q,"asr_text",q->text);
    return error;
}
void agent_asr_realtime_cancel(agent_asr_realtime_t *q)
{ (void)fail(q,AGENT_ERR_CANCELLED); }

__attribute__((noinline)) static agent_err_t live_buffer(agent_asr_realtime_t *q,
    const agent_speech_live_t *input,int16_t *block)
{
    agent_err_t error=AGENT_OK;bool end=false,started=false;
    unsigned chunks=0;uint64_t source_ms=0,feed_ms=0,wait_ms=0;
    while(!error && !end) {
        error=guard(q);if(error)break;
        size_t count=0;uint64_t began=q->speech->now_ms(q->speech->clock_ctx);
        error=input->next(input->ctx,block,AGENT_ASR_RT_CHUNK,&count,&end);
        source_ms+=q->speech->now_ms(q->speech->clock_ctx)-began;
        if(!error && count>AGENT_ASR_RT_CHUNK)error=AGENT_ERR_LIMIT;
        if(!error && count) {
            if(!started) {event(q,"asr_audio",NULL);started=true;}
            began=q->speech->now_ms(q->speech->clock_ctx);
            error=agent_asr_realtime_feed_pcm(q,block,count);
            feed_ms+=q->speech->now_ms(q->speech->clock_ctx)-began;
            if(!error)++chunks;
        } else if(!error && !end) {
            began=q->speech->now_ms(q->speech->clock_ctx);
            error=agent_asr_realtime_poll(q,20);
            wait_ms+=q->speech->now_ms(q->speech->clock_ctx)-began;
        }
    }
    /* No pending frame or borrowed PCM survives send_json. The synchronous
     * event borrows TX; finish can reuse it only after the callback returns. */
    char *stats=q->speech->scratch+AGENT_ASR_RT_EVENT_MAX;
    snprintf(stats,192,"{\"samples\":%u,\"chunks\":%u,\"source_ms\":%llu,\"feed_ms\":%llu,\"wait_ms\":%llu,\"complete\":%s}",
        (unsigned)q->input_samples,chunks,(unsigned long long)source_ms,
        (unsigned long long)feed_ms,(unsigned long long)wait_ms,end&&!error?"true":"false");
    event(q,"asr_upload_stats",stats);
    if(!error) {event(q,"vad_end",NULL);error=agent_asr_realtime_finish(q);}
    return fail(q,error);
}
/* LTO must not lift this recorded-mode array into the live/LLM worker frame. */
__attribute__((noinline)) agent_err_t agent_asr_realtime_live(agent_asr_realtime_t *q,const agent_speech_live_t *input)
{
    if(!input || !input->next)return AGENT_ERR_ARGUMENT;
    agent_err_t error=agent_asr_realtime_begin(q);if(error)return error;
    int16_t block[AGENT_ASR_RT_CHUNK];return live_buffer(q,input,block);
}
static bool overlap(const void *a,size_t n,const void *b,size_t m)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return n && m && (x<=y?y-x<n:x-y<m);
}
agent_err_t agent_asr_realtime_live_shared(agent_asr_realtime_t *q,const agent_speech_live_t *input)
{
    if(!input || !input->next || !q || !q->speech || !q->speech->scratch ||
       q->speech->capacity<AGENT_ASR_RT_SCRATCH ||
       (uintptr_t)q->speech->scratch%_Alignof(int16_t))return AGENT_ERR_ARGUMENT;
    char *scratch=q->speech->scratch;
    if(overlap(scratch,AGENT_ASR_RT_SCRATCH,q,sizeof(*q)) ||
       overlap(scratch,AGENT_ASR_RT_SCRATCH,q->speech,sizeof(*q->speech)) ||
       overlap(scratch,AGENT_ASR_RT_SCRATCH,input,sizeof(*input)) ||
       overlap(scratch,AGENT_ASR_RT_SCRATCH,q->text,q->capacity) ||
       (q->ws && overlap(scratch,AGENT_ASR_RT_SCRATCH,q->ws,sizeof(*q->ws))))return AGENT_ERR_ARGUMENT;
    /* Longest prefix is <=80B (including the32-bit nonce). For forward
     * base64 expansion the last read/write gap is still >700B; TX1120B
     * offset therefore never overwrites unread source. Count stays<=464. */
    _Static_assert(80+4*((AGENT_ASR_RT_CHUNK*2+2)/3)+2<
        AGENT_ASR_RT_SCRATCH-AGENT_ASR_RT_EVENT_MAX,"TX must fit the complete frame");
    _Static_assert(80+(AGENT_ASR_RT_CHUNK*2+2)/3+4<
        AGENT_ASR_RT_SCRATCH-AGENT_ASR_RT_EVENT_MAX-AGENT_ASR_RT_CHUNK*2,
        "Forward encoder must stay behind unread PCM");
    agent_err_t error=agent_asr_realtime_begin(q);if(error)return error;
    return live_buffer(q,input,(int16_t *)(q->speech->scratch+AGENT_ASR_RT_SCRATCH-AGENT_ASR_RT_CHUNK*2));
}
typedef struct { const agent_speech_input_t *input;size_t offset; } recorded_input_t;
static agent_err_t recorded_next(void *ctx,int16_t *out,size_t capacity,size_t *count,bool *end)
{
    recorded_input_t *r=ctx;size_t n=r->input->samples-r->offset;
    if(n>capacity)n=capacity;
    agent_err_t error=n?r->input->read(r->input->ctx,r->offset,out,n):AGENT_OK;
    if(!error)r->offset+=n;
    *count=error?0:n;*end=r->offset==r->input->samples;return error;
}
agent_err_t agent_asr_realtime_transcribe(agent_asr_realtime_t *q,const agent_speech_input_t *input)
{
    if(!input || !input->read || !input->samples || input->samples>AGENT_ASR_RT_INPUT_MAX)return AGENT_ERR_ARGUMENT;
    recorded_input_t reader={input,0};agent_speech_live_t live={recorded_next,&reader};
    return agent_asr_realtime_live(q,&live);
}
