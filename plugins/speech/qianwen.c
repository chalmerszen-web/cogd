#include "qianwen.h"
#include "json.h"
#include <stdio.h>
#include <string.h>

static agent_err_t guard(agent_qwen_t *q)
{
    agent_speech_t *s=q->speech;
    if(s->cancelled && atomic_load(s->cancelled)) return AGENT_ERR_CANCELLED;
    return s->now_ms(s->clock_ctx)>=q->deadline?AGENT_ERR_TIMEOUT:AGENT_OK;
}
static void event(agent_qwen_t *q,const char *stage,const char *text)
{ agent_speech_t *s=q->speech;if(s->event)s->event(s->event_ctx,stage,text); }
static void sentence_notice(agent_qwen_t *q,const cJSON *sentence,uint32_t id,bool final,bool nonempty)
{
    agent_speech_t *s=q->speech;if(!s->asr_sentence)return;
    uint64_t begin=0,end=0;
    bool timed=agent_json_uint(cJSON_GetObjectItemCaseSensitive(sentence,"begin_time"),10000,&begin) &&
               agent_json_uint(cJSON_GetObjectItemCaseSensitive(sentence,"end_time"),10000,&end) &&
               end && begin<=end;
    s->asr_sentence(s->asr_sentence_ctx,id,timed?(unsigned)end:0,final,nonempty,timed);
}
void agent_qwen_init(agent_qwen_t *q,agent_speech_t *s,const agent_ws_ops_t *ws)
{ memset(q,0,sizeof(*q));q->speech=s;q->ws=ws;s->provider_state=q; }
static bool overlap(const void *a,size_t n,const void *b,size_t m)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return n && m && (x<=y?y-x<n:x-y<m);
}
agent_err_t agent_qwen_use_rx_pcm(agent_qwen_t *q)
{
    if(!q || !q->speech || !q->speech->scratch || q->speech->capacity<AGENT_QWEN_SCRATCH ||
       (uintptr_t)q->speech->scratch%_Alignof(int16_t))return AGENT_ERR_ARGUMENT;
    if(overlap(q->speech->scratch,AGENT_QWEN_SCRATCH,q,sizeof(*q)) ||
       overlap(q->speech->scratch,AGENT_QWEN_SCRATCH,q->speech,sizeof(*q->speech)) ||
       (q->ws && overlap(q->speech->scratch,AGENT_QWEN_SCRATCH,q->ws,sizeof(*q->ws))))return AGENT_ERR_ARGUMENT;
    if(q->opened || q->started || q->message || q->transcript || q->rx_pcm)return AGENT_ERR_BUSY;
    q->rx_pcm=true;return AGENT_OK;
}
static void close_session(agent_qwen_t *q)
{ if(q->opened)q->ws->close(q->ws->ctx);q->opened=false; }
static agent_err_t provider_error(const char *code)
{
    if(!code)return AGENT_ERR_SERVER;
    if(strstr(code,"ApiKey") || strstr(code,"Unauthorized"))return AGENT_ERR_AUTH;
    if(strstr(code,"Forbidden") || strstr(code,"AccessDenied"))return AGENT_ERR_FORBIDDEN;
    if(strstr(code,"Throttl") || strstr(code,"Limit") || strstr(code,"RATE"))return AGENT_ERR_RATE;
    if(strstr(code,"Parameter") || strstr(code,"Invalid"))return AGENT_ERR_ARGUMENT;
    if(strstr(code,"Timeout") || strstr(code,"TIMEOUT"))return AGENT_ERR_TIMEOUT;
    return AGENT_ERR_SERVER;
}
static agent_err_t text_event(agent_qwen_t *q)
{
    cJSON *root=agent_json_parse(q->speech->scratch,q->used);
    if(!root)return AGENT_ERR_JSON;
    const cJSON *header=cJSON_GetObjectItemCaseSensitive(root,"header");
    const char *id=agent_json_string(header,"task_id"),*kind=agent_json_string(header,"event");
    agent_err_t error=AGENT_OK;
    if(!id || strcmp(id,q->speech->task_id) || !kind)error=AGENT_ERR_PROTOCOL;
    else if(!strcmp(kind,"task-failed")) {
        const char *code=agent_json_string(header,"error_code");error=provider_error(code);
        if(code && strlen(code)<96 && strspn(code,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")==strlen(code))
            event(q,"provider_error",code);
    } else if(!strcmp(kind,"task-started")) {
        if(q->started || q->ending)error=AGENT_ERR_PROTOCOL;
        else {q->started=true;event(q,q->asr?"asr_started":"tts_started",NULL);}
    } else if(!q->started || q->done)error=AGENT_ERR_PROTOCOL;
    else if(!strcmp(kind,"task-finished")) {
        if(!q->ending || q->message || q->odd)error=AGENT_ERR_PROTOCOL;
        else {q->done=true;event(q,q->asr?"asr_done":"tts_done",NULL);}
    } else if(!strcmp(kind,"result-generated")) {
        if(q->asr) {
            const cJSON *payload=cJSON_GetObjectItemCaseSensitive(root,"payload");
            const cJSON *output=cJSON_GetObjectItemCaseSensitive(payload,"output");
            const cJSON *sentence=cJSON_GetObjectItemCaseSensitive(output,"sentence");
            if(!cJSON_IsObject(sentence))error=AGENT_ERR_PROTOCOL;
            else if(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(sentence,"heartbeat"))) {
                const char *text=agent_json_string(sentence,"text");uint64_t number=0;
                if(!text || !agent_json_uint(cJSON_GetObjectItemCaseSensitive(sentence,"sentence_id"),UINT32_MAX,&number) || !number)
                    error=AGENT_ERR_PROTOCOL;
                else if(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(sentence,"sentence_end"))) {
                    /* Server finals are monotone; repeats never append twice. */
                    if(number>q->sentence) {
                        size_t length=strlen(text);
                        if(!q->transcript || length>=q->transcript_cap-q->transcript_used)error=AGENT_ERR_LIMIT;
                        else {
                            memcpy(q->transcript+q->transcript_used,text,length+1);q->transcript_used+=length;
                            q->sentence=(uint32_t)number;
                            sentence_notice(q,sentence,(uint32_t)number,true,length!=0);
                            event(q,"asr_final",text);
                        }
                    }
                } else {
                    sentence_notice(q,sentence,(uint32_t)number,false,*text!=0);
                    if(*text && !q->partial) {q->partial=true;event(q,"asr_partial",text);}
                }
            }
        }
    } else error=AGENT_ERR_PROTOCOL;
    cJSON_Delete(root);return error;
}
static agent_err_t pcm_buffer(agent_qwen_t *q,const uint8_t *p,size_t n,int16_t *block)
{
    size_t used=0;
    while(n--) {
        if(!q->odd) {q->low_byte=*p++;q->odd=true;continue;}
        block[used++]=(int16_t)((uint16_t)q->low_byte|(uint16_t)*p++<<8);q->odd=false;
        if(used==256) {agent_err_t e=q->sink.write(q->sink.ctx,block,used);if(e)return e;used=0;}
    }
    return used?q->sink.write(q->sink.ctx,block,used):AGENT_OK;
}
/* Keep the legacy typed array out of the explicit-loan call path. */
__attribute__((noinline)) static agent_err_t pcm_stack(agent_qwen_t *q,const uint8_t *p,size_t n)
{ int16_t block[256];return pcm_buffer(q,p,n,block); }
static agent_err_t pcm(agent_qwen_t *q,const uint8_t *p,size_t n)
{
    if(q->asr || !q->started || q->done)return AGENT_ERR_PROTOCOL;
    if(n>24000u*2u*90u-q->pcm_bytes)return AGENT_ERR_LIMIT;
    if(q->rx_pcm && overlap(p,n,q->speech->scratch,512))return AGENT_ERR_ARGUMENT;
    if(!q->pcm_open && n) {
        agent_err_t e=q->sink.open(q->sink.ctx,24000);if(e)return e;q->pcm_open=true;
    }
    q->pcm_bytes+=n;
    return q->rx_pcm?pcm_buffer(q,p,n,(int16_t *)q->speech->scratch):pcm_stack(q,p,n);
}
agent_err_t agent_qwen_receive(agent_qwen_t *q,const char *data,const agent_ws_chunk_t *part)
{
    agent_err_t error=guard(q);if(error)return error;
    if(part->first && part->opcode) {
        if(q->message || (part->opcode!=1 && part->opcode!=2))return AGENT_ERR_PROTOCOL;
        q->opcode=part->opcode;q->used=0;q->message=true;
    } else if(!q->message)return AGENT_ERR_PROTOCOL;
    if(q->opcode==1) {
        if(part->length>=AGENT_QWEN_EVENT_MAX-q->used)return AGENT_ERR_LIMIT;
        memcpy(q->speech->scratch+q->used,data,part->length);q->used+=part->length;
        q->speech->scratch[q->used]=0;
    } else error=pcm(q,(const uint8_t *)data,part->length);
    if(!error && part->final) {
        q->message=false;
        if(q->opcode==1)error=text_event(q);
        q->used=0;
    }
    return error;
}
static agent_err_t poll_session(agent_qwen_t *q,unsigned ms)
{
    agent_err_t error=guard(q);if(error)return error;
    char bytes[1024];agent_ws_chunk_t part={0};
    error=q->ws->read(q->ws->ctx,bytes,sizeof(bytes),&part,ms,q->speech->cancelled);
    return !error && (part.length || part.first)?agent_qwen_receive(q,bytes,&part):error;
}
agent_err_t agent_qwen_poll(agent_qwen_t *q,unsigned ms)
{ return poll_session(q,ms); }
static void writer(agent_qwen_t *q,agent_json_writer_t *w,const char *action)
{
    agent_json_writer_init(w,q->speech->scratch+AGENT_QWEN_EVENT_MAX,AGENT_QWEN_SCRATCH-AGENT_QWEN_EVENT_MAX);
    agent_json_raw(w,"{\"header\":{\"action\":");agent_json_quote(w,action);
    agent_json_raw(w,",\"task_id\":");agent_json_quote(w,q->speech->task_id);
    agent_json_raw(w,",\"streaming\":\"duplex\"},\"payload\":");
}
static agent_err_t send_chunk(agent_qwen_t *q,bool binary,char *data,size_t length)
{
    /* Both peers can fill their TCP windows. While the unsent frame waits for
     * capacity, drain provider events instead of deadlocking on a write poll.
     * BUSY is allowed only before the adapter writes ANY frame bytes. */
    for(;;) {
        agent_err_t error=guard(q);if(error)return error;
        error=q->ws->send(q->ws->ctx,binary,data,length,q->speech->cancelled);
        if(error!=AGENT_ERR_BUSY)return error;
        error=poll_session(q,20);if(error)return error;
    }
}
static agent_err_t send_json(agent_qwen_t *q,agent_json_writer_t *w)
{
    if(w->error)return w->error;
    return send_chunk(q,false,q->speech->scratch+AGENT_QWEN_EVENT_MAX,w->used);
}
static agent_err_t start(agent_speech_t *s,bool asr,const char *voice,const agent_pcm_sink_t *sink)
{
    agent_qwen_t *q=s?s->provider_state:NULL;
    if(!q || !q->ws || !s->now_ms || s->capacity<AGENT_QWEN_SCRATCH || q->opened)return AGENT_ERR_CONFIG;
    if(asr && q->rx_pcm)return AGENT_ERR_ARGUMENT;
    const agent_ws_ops_t *ws=q->ws;char *out=q->transcript;size_t cap=q->transcript_cap;bool rx_pcm=q->rx_pcm;
    agent_qwen_init(q,s,ws);q->asr=asr;q->transcript=out;q->transcript_cap=cap;
    q->rx_pcm=rx_pcm;
    if(asr && (!out || !cap))return AGENT_ERR_ARGUMENT;
    if(asr)out[0]=0;
    q->deadline=s->now_ms(s->clock_ctx)+(asr?35000:95000);
    agent_err_t error=guard(q);if(error)return error;
    if(sink)q->sink=*sink;
    uint32_t id[4];for(unsigned i=0;i<4;++i)id[i]=s->random_u32?s->random_u32():s->nonce+i+(uint32_t)s->now_ms(s->clock_ctx);
    snprintf(s->task_id,sizeof(s->task_id),"%08lx%08lx%08lx%08lx",(unsigned long)id[0],(unsigned long)id[1],(unsigned long)id[2],(unsigned long)id[3]);
    event(q,asr?"asr_connect":"tts_connect",s->task_id);
    error=ws->open(ws->ctx,s->cancelled);if(error)return error;q->opened=true;
    event(q,asr?"asr_connected":"tts_connected",NULL);
    agent_json_writer_t w;writer(q,&w,"run-task");
    if(asr)agent_json_raw(&w,"{\"task_group\":\"audio\",\"task\":\"asr\",\"function\":\"recognition\",\"model\":\"" AGENT_QWEN_ASR_MODEL "\",\"parameters\":{\"format\":\"pcm\",\"sample_rate\":16000,\"max_sentence_silence\":800,\"semantic_punctuation_enabled\":false},\"input\":{}}}");
    else {
        agent_json_raw(&w,"{\"task_group\":\"audio\",\"task\":\"tts\",\"function\":\"SpeechSynthesizer\",\"model\":\"" AGENT_QWEN_TTS_MODEL "\",\"parameters\":{\"text_type\":\"PlainText\",\"voice\":");
        agent_json_quote(&w,voice);agent_json_raw(&w,",\"format\":\"pcm\",\"sample_rate\":24000},\"input\":{}}}");
    }
    error=send_json(q,&w);uint64_t until=s->now_ms(s->clock_ctx)+10000;
    while(!error && !q->started) {
        error=poll_session(q,50);if(!error && s->now_ms(s->clock_ctx)>=until)error=AGENT_ERR_TIMEOUT;
    }
    if(error)close_session(q);
    return error;
}
static agent_err_t finish(agent_qwen_t *q)
{
    if(!q->opened || !q->started || q->ending)return AGENT_ERR_PROTOCOL;
    agent_json_writer_t w;writer(q,&w,"finish-task");agent_json_raw(&w,"{\"input\":{}}}");
    q->ending=true;event(q,q->asr?"asr_finish":"tts_finish",NULL);
    agent_err_t error=send_json(q,&w);
    while(!error && !q->done)error=poll_session(q,50);
    if(!error && (q->message || q->odd || (q->asr?!q->transcript_used:!q->pcm_bytes)))error=AGENT_ERR_NOT_FOUND;
    close_session(q);return error;
}
static void cancel(agent_speech_t *s)
{ if(s && s->provider_state)close_session(s->provider_state); }
static agent_err_t asr_begin(agent_speech_t *s,unsigned rate)
{ return rate==16000?start(s,true,NULL,NULL):AGENT_ERR_ARGUMENT; }
static agent_err_t asr_feed(agent_speech_t *s,const int16_t *pcm_data,size_t count)
{
    agent_qwen_t *q=s->provider_state;if(!q || !q->asr || !q->started || q->ending)return AGENT_ERR_PROTOCOL;
    if(!pcm_data && count)return AGENT_ERR_ARGUMENT;
    char *bytes=s->scratch+AGENT_QWEN_EVENT_MAX;
    while(count) {
        size_t n=count>1024?1024:count;
        for(size_t i=0;i<n;++i) {bytes[2*i]=(char)pcm_data[i];bytes[2*i+1]=(char)((uint16_t)pcm_data[i]>>8);}
        agent_err_t e=send_chunk(q,true,bytes,n*2);
        if(!e)e=poll_session(q,0);
        if(e)return e;
        count-=n;pcm_data+=n;
    }
    return AGENT_OK;
}
static agent_err_t asr_finish(agent_speech_t *s,char *out,size_t capacity)
{
    agent_qwen_t *q=s->provider_state;
    if(!q || !q->asr || out!=q->transcript || capacity!=q->transcript_cap)return AGENT_ERR_ARGUMENT;
    agent_err_t e=finish(q);if(!e)event(q,"asr_text",out);return e;
}
agent_err_t agent_qwen_live(agent_speech_t *s,const agent_speech_live_t *input,char *out,size_t capacity)
{
    if(!input || !input->next || !out || !capacity)return AGENT_ERR_ARGUMENT;
    agent_qwen_t *q=s?s->provider_state:NULL;if(!q)return AGENT_ERR_CONFIG;
    q->transcript=out;q->transcript_cap=capacity;
    agent_err_t e=asr_begin(s,16000);
    if(e)return e;
    int16_t block[1024];bool end=false;bool sent=false;
    uint64_t samples=0,chunks=0,source_ms=0,feed_ms=0,wait_ms=0;
    while(!e && !end) {
        uint64_t began=s->now_ms(s->clock_ctx);
        size_t n=0;e=input->next(input->ctx,block,1024,&n,&end);
        source_ms+=s->now_ms(s->clock_ctx)-began;
        if(n>1024)e=AGENT_ERR_LIMIT;
        if(!e && n) {
            if(!sent) {event(q,"asr_audio",NULL);sent=true;}
            began=s->now_ms(s->clock_ctx);
            e=asr_feed(s,block,n);
            feed_ms+=s->now_ms(s->clock_ctx)-began;
            /* A failed call may have sent PCM before a receive/cancel error.
             * Count only whole successful feeds, not ambiguous wire bytes. */
            if(!e) {samples+=n;++chunks;}
        }
        if(!e && !n && !end) {
            began=s->now_ms(s->clock_ctx);e=poll_session(q,20);
            wait_ms+=s->now_ms(s->clock_ctx)-began;
        }
    }
    /* The PCM block is no longer borrowed; reuse it for one bounded diagnostic.
     * complete is local EOF + upload success, not a server acknowledgement. */
    snprintf((char *)block,192,"{\"samples\":%llu,\"chunks\":%llu,\"source_ms\":%llu,\"feed_ms\":%llu,\"wait_ms\":%llu,\"complete\":%s}",
        (unsigned long long)samples,(unsigned long long)chunks,(unsigned long long)source_ms,
        (unsigned long long)feed_ms,(unsigned long long)wait_ms,end&&!e?"true":"false");
    event(q,"asr_upload_stats",(const char *)block);
    if(!e) {event(q,"vad_end",NULL);e=asr_finish(s,out,capacity);}
    if(e)cancel(s);
    return e;
}
typedef struct { const agent_speech_input_t *input;size_t offset; } clip_input_t;
static agent_err_t clip_next(void *ctx,int16_t *out,size_t cap,size_t *count,bool *end)
{
    clip_input_t *r=ctx;size_t n=r->input->samples-r->offset;if(n>cap)n=cap;
    agent_err_t e=n?r->input->read(r->input->ctx,r->offset,out,n):AGENT_OK;
    if(!e)r->offset+=n;
    *count=e?0:n;*end=r->offset==r->input->samples;return e;
}
static agent_err_t transcribe(agent_speech_t *s,const agent_speech_input_t *input,char *out,size_t cap)
{
    if(!input || !input->read || !input->samples || input->samples>160000)return AGENT_ERR_ARGUMENT;
    clip_input_t r={input,0};agent_speech_live_t live={clip_next,&r};return agent_qwen_live(s,&live,out,cap);
}
static agent_err_t tts_begin(agent_speech_t *s,const char *voice,const agent_pcm_sink_t *sink)
{
    if(!voice || !*voice || strlen(voice)>96 || !sink || !sink->open || !sink->write)return AGENT_ERR_ARGUMENT;
    return start(s,false,voice,sink);
}
static agent_err_t tts_feed(agent_speech_t *s,const char *text,size_t length)
{
    agent_qwen_t *q=s->provider_state;
    if(!q || q->asr || !q->started || q->ending)return AGENT_ERR_PROTOCOL;
    if(!text || !agent_utf8_valid(text,length))return AGENT_ERR_ARGUMENT;
    if(length>AGENT_SPEECH_TEXT_MAX-q->text_sent)return AGENT_ERR_LIMIT;
    q->text_sent+=length;
    while(length) {
        size_t n=length>256?256:length;while(n<length && ((unsigned char)text[n]&0xc0)==0x80)--n;
        char chunk[257];memcpy(chunk,text,n);chunk[n]=0;
        agent_json_writer_t w;writer(q,&w,"continue-task");agent_json_raw(&w,"{\"input\":{\"text\":");
        agent_json_quote(&w,chunk);agent_json_raw(&w,"}}}");
        agent_err_t e=send_json(q,&w);if(!e)e=poll_session(q,0);if(e)return e;
        text+=n;length-=n;
    }
    return AGENT_OK;
}
static agent_err_t tts_finish(agent_speech_t *s)
{ agent_qwen_t *q=s->provider_state;return q && !q->asr && q->text_sent?finish(q):AGENT_ERR_ARGUMENT; }
static agent_err_t speak(agent_speech_t *s,const char *voice,const char *text,const agent_pcm_sink_t *sink)
{
    if(!text || !*text || strlen(text)>AGENT_SPEECH_TEXT_MAX)return AGENT_ERR_LIMIT;
    agent_err_t e=tts_begin(s,voice,sink);
    if(!e)e=tts_feed(s,text,strlen(text));
    if(!e)e=tts_finish(s);
    if(e)cancel(s);
    return e;
}
const agent_asr_ops_t agent_qianwen_asr={true,transcribe,asr_begin,asr_feed,asr_finish,cancel};
const agent_tts_ops_t agent_qianwen_tts={true,speak,tts_begin,tts_feed,tts_finish,cancel};
