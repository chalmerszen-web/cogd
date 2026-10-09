#include "speech.h"
#include "json.h"
#include <stdio.h>
#include <string.h>

static bool cancelled(agent_speech_t *s)
{ return s->cancelled && atomic_load(s->cancelled); }
static void event(agent_speech_t *s,const char *stage,const char *text)
{ if(s->event) s->event(s->event_ctx,stage,text); }
static agent_err_t collect(void *ctx,const char *data,size_t n)
{
    agent_speech_t *s=ctx;
    if(n>=s->capacity-s->used) return AGENT_ERR_LIMIT;
    memcpy(s->scratch+s->used,data,n); s->used+=n; s->scratch[s->used]=0;
    return AGENT_OK;
}
static agent_err_t json_request(agent_speech_t *s,agent_http_target_t target,const char *method,const char *path,size_t length)
{
    if(cancelled(s)) return AGENT_ERR_CANCELLED;
    agent_http_request_t r={.target=target,.method=method,.path=path,.body=s->scratch,
        .length=length,.cancelled=s->cancelled,.reuse_connection=target==AGENT_HTTP_VOCALIGN};
    s->used=0;
    return s->transport->perform(s->transport->ctx,&r,collect,s);
}
static bool copy_field(const cJSON *root,const char *key,char *out,size_t cap)
{
    const char *value=agent_json_string(root,key);
    if(!value || !*value || strlen(value)>=cap) return false;
    strcpy(out,value); return true;
}
static bool safe_id(const char *s)
{ return *s && strspn(s,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")==strlen(s); }
static agent_err_t field(agent_json_writer_t *w,const char *name,const char *value)
{
    if(!value || strchr(value,'\r') || strchr(value,'\n')) return AGENT_ERR_PROTOCOL;
    agent_json_raw(w,"--agent-speech\r\nContent-Disposition: form-data; name=\"");
    agent_json_raw(w,name); agent_json_raw(w,"\"\r\n\r\n");
    agent_json_raw(w,value); agent_json_raw(w,"\r\n"); return w->error;
}
typedef struct { const agent_speech_input_t *input; const char *prefix; size_t length; } upload_t;
static const char upload_end[]="\r\n--agent-speech--\r\n";
static agent_err_t upload_body(void *ctx,agent_write_fn write,void *dst)
{
    upload_t *u=ctx; uint8_t header[44]; int16_t block[1024];
    agent_wav_header(header,16000,u->input->samples);
    agent_err_t e=write(dst,u->prefix,u->length);
    if(!e) e=write(dst,(const char *)header,sizeof(header));
    for(size_t at=0;!e && at<u->input->samples;) {
        size_t n=u->input->samples-at; if(n>1024) n=1024;
        e=u->input->read(u->input->ctx,at,block,n);
        if(!e) e=write(dst,(const char *)block,n*2);
        at+=n;
    }
    return e?e:write(dst,upload_end,sizeof(upload_end)-1);
}
static agent_err_t upload_clip(agent_speech_t *s,const agent_speech_input_t *input,char *oss,size_t oss_cap)
{
    agent_err_t e=json_request(s,AGENT_HTTP_VOCALIGN,"GET","/v1/uploads?action=getPolicy&model=qwen-asr",0);
    if(e) return e;
    cJSON *p=agent_json_parse(s->scratch,s->used);
    char url[768],key[512];
    const char *directory=agent_json_string(p,"upload_dir");
    uint64_t max_mb=0,capacity_mb=0;
    if(!directory || !*directory || strlen(directory)>400 ||
       strspn(directory,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._/-")!=strlen(directory) || strstr(directory,"..") ||
       !copy_field(p,"upload_host",url,sizeof(url)) || strncmp(url,"https://",8) ||
       !agent_json_uint(cJSON_GetObjectItemCaseSensitive(p,"max_file_size_mb"),UINT32_MAX,&max_mb) || !max_mb ||
       !agent_json_uint(cJSON_GetObjectItemCaseSensitive(p,"capacity_limit_mb"),UINT32_MAX,&capacity_mb) || !capacity_mb)
        { cJSON_Delete(p); return AGENT_ERR_PROTOCOL; }
    uint64_t file_size=44+(uint64_t)input->samples*2;
    if(file_size>max_mb*1024*1024 || file_size>capacity_mb*1024*1024) { cJSON_Delete(p); return AGENT_ERR_LIMIT; }
    while(*directory=='/') ++directory;
    int n=snprintf(key,sizeof(key),"%s/%08lx-%08lx.wav",directory,(unsigned long)s->nonce,(unsigned long)s->now_ms(s->clock_ctx));
    if(n<0 || (size_t)n>=sizeof(key) || snprintf(oss,oss_cap,"oss://%s",key)>=(int)oss_cap) { cJSON_Delete(p); return AGENT_ERR_LIMIT; }
    agent_json_writer_t w; agent_json_writer_init(&w,s->scratch,s->capacity);
    const char *src[]={"oss_access_key_id","signature","policy","x_oss_object_acl","x_oss_forbid_overwrite"};
    const char *dst[]={"OSSAccessKeyId","Signature","policy","x-oss-object-acl","x-oss-forbid-overwrite"};
    for(unsigned i=0;!e && i<5;++i) e=field(&w,dst[i],agent_json_string(p,src[i]));
    if(!e) e=field(&w,"key",key);
    if(!e) e=field(&w,"success_action_status","200");
    agent_json_raw(&w,"--agent-speech\r\nContent-Disposition: form-data; name=\"file\"; filename=\"utterance.wav\"\r\nContent-Type: audio/wav\r\n\r\n");
    if(!e) e=w.error;
    cJSON_Delete(p); if(e) return e;
    upload_t body={.input=input,.prefix=s->scratch,.length=w.used};
    agent_http_request_t r={.target=AGENT_HTTP_PUBLIC,.method="POST",.path=url,
        .content_type="multipart/form-data; boundary=agent-speech",.length=w.used+(size_t)file_size+sizeof(upload_end)-1,
        .produce=upload_body,.body_ctx=&body,.cancelled=s->cancelled};
    s->used=0;
    event(s,"upload_send",NULL);
    return s->transport->perform(s->transport->ctx,&r,collect,s);
}
agent_err_t agent_speech_transcript(const char *json,size_t length,char *out,size_t cap)
{
    cJSON *root=agent_json_parse(json,length); if(!root) return AGENT_ERR_JSON;
    const cJSON *rows=cJSON_GetObjectItemCaseSensitive(root,"transcripts");
    agent_err_t e=cJSON_IsArray(rows)?AGENT_OK:AGENT_ERR_PROTOCOL;
    size_t used=0; if(cap) out[0]=0;
    const cJSON *row;
    cJSON_ArrayForEach(row,rows) {
        const char *text=agent_json_string(row,"text");
        if(!text || !agent_utf8_valid(text,strlen(text))) {e=AGENT_ERR_PROTOCOL;break;}
        size_t n=strlen(text); if(n+used+(used!=0)>=cap) {e=AGENT_ERR_LIMIT;break;}
        if(used && n) out[used++]=' ';
        memcpy(out+used,text,n+1); used+=n;
    }
    cJSON_Delete(root);
    if(!e && !used) e=AGENT_ERR_NOT_FOUND;
    return e;
}
static agent_err_t transcribe(agent_speech_t *s,const agent_speech_input_t *input,char *out,size_t cap)
{
    if(!input || !input->read || !input->samples || input->samples>160000 || cap<2) return AGENT_ERR_ARGUMENT;
    s->task_id[0]=0; event(s,"upload",NULL);
    char address[1536]; agent_err_t e=upload_clip(s,input,address,sizeof(address)); if(e) return e;
    agent_json_writer_t w; agent_json_writer_init(&w,s->scratch,s->capacity);
    agent_json_raw(&w,"{\"model\":\"qwen-asr\",\"file_url\":"); agent_json_quote(&w,address);
    char tail[96]; snprintf(tail,sizeof(tail),",\"duration\":%lu,\"diarization_enabled\":false}",(unsigned long)((input->samples+15999)/16000));
    agent_json_raw(&w,tail); if(w.error) return w.error;
    event(s,"asr_submit",NULL);
    e=json_request(s,AGENT_HTTP_VOCALIGN,"POST","/v1/audio/transcriptions",w.used); if(e) return e;
    cJSON *r=agent_json_parse(s->scratch,s->used);
    bool valid=copy_field(r,"id",s->task_id,sizeof(s->task_id)) && safe_id(s->task_id);
    cJSON_Delete(r); if(!valid) return AGENT_ERR_PROTOCOL;
    event(s,"asr_wait",s->task_id);
    char path[192]; snprintf(path,sizeof(path),"/v1/audio/transcriptions/%s",s->task_id);
    uint64_t started=s->now_ms(s->clock_ctx),end=started+180000; unsigned failures=0;
    for(;;) {
        uint64_t elapsed=s->now_ms(s->clock_ctx)-started;
        unsigned delay=failures?2000:elapsed<8000?1000:elapsed<30000?2000:5000;
        for(unsigned waited=0;waited<delay;waited+=100) {
            if(cancelled(s)) return AGENT_ERR_CANCELLED;
            if(s->now_ms(s->clock_ctx)>=end) return AGENT_ERR_TIMEOUT;
            s->wait_ms(100);
        }
        if(s->now_ms(s->clock_ctx)>=end) return AGENT_ERR_TIMEOUT;
        e=json_request(s,AGENT_HTTP_VOCALIGN,"GET",path,0);
        if(e) {
            if((e==AGENT_ERR_RATE || e==AGENT_ERR_SERVER || e==AGENT_ERR_NETWORK || e==AGENT_ERR_TIMEOUT) && ++failures<=3) continue;
            return e;
        }
        failures=0; r=agent_json_parse(s->scratch,s->used);
        const char *state=agent_json_string(r,"status");
        bool done=state && !strcmp(state,"completed");
        if(done) valid=copy_field(r,"transcription_url",address,sizeof(address)) && !strncmp(address,"https://",8);
        else valid=state && (!strcmp(state,"queued") || !strcmp(state,"in_progress"));
        bool failed=state && !strcmp(state,"failed");
        if(valid) event(s,done?"asr_ready":"asr_poll",done?NULL:state);
        cJSON_Delete(r);
        if(failed) return AGENT_ERR_SERVER;
        if(!valid) return AGENT_ERR_PROTOCOL;
        if(done) break;
    }
    e=json_request(s,AGENT_HTTP_PUBLIC,"GET",address,0);
    if(!e) e=agent_speech_transcript(s->scratch,s->used,out,cap);
    if(!e) event(s,"asr_text",out);
    return e;
}
static agent_err_t speak(agent_speech_t *s,const char *voice,const char *text,const agent_pcm_sink_t *sink)
{
    if(cancelled(s)) return AGENT_ERR_CANCELLED;
    if(!voice || !safe_id(voice) || !text || !*text || !sink || !sink->open || !sink->write) return AGENT_ERR_ARGUMENT;
    if(strlen(text)>AGENT_SPEECH_TEXT_MAX || !agent_utf8_valid(text,strlen(text))) return AGENT_ERR_LIMIT;
    agent_json_writer_t w; agent_json_writer_init(&w,s->scratch,s->capacity);
    agent_json_raw(&w,"{\"model\":\"openai-tts\",\"response_format\":\"wav\",\"speed\":1.0,\"voice\":");
    agent_json_quote(&w,voice); agent_json_raw(&w,",\"input\":"); agent_json_quote(&w,text); agent_json_raw(&w,"}");
    if(w.error) return w.error;
    agent_http_request_t r={.target=AGENT_HTTP_VOCALIGN,.method="POST",.path="/v1/audio/speech",
        .body=s->scratch,.length=w.used,.cancelled=s->cancelled};
    agent_wav_t wav; agent_wav_init(&wav,sink); event(s,"tts",NULL);
    agent_err_t e=s->transport->perform(s->transport->ctx,&r,agent_wav_feed,&wav);
    if(!e) e=agent_wav_finish(&wav);
    return e;
}
const agent_asr_ops_t agent_vocalign_asr={.transcribe=transcribe};
const agent_tts_ops_t agent_vocalign_tts={.speak=speak};
