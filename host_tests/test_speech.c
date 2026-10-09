#include "speech.h"
#include "intent.h"
#include "json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { int16_t data[1024]; size_t count; unsigned opens; bool fail; } sink_t;
static agent_err_t open_pcm(void *ctx,unsigned rate)
{ sink_t *s=ctx;assert(rate==24000);++s->opens;return AGENT_OK; }
static agent_err_t write_pcm(void *ctx,const int16_t *pcm,size_t n)
{
    sink_t *s=ctx;if(s->fail) return AGENT_ERR_CANCELLED;
    assert(s->count+n<=1024);memcpy(s->data+s->count,pcm,n*2);s->count+=n;return AGENT_OK;
}
static void wav_checks(void)
{
    assert(agent_pcm_gain(-10000,80)==-8000 && agent_pcm_gain(10000,80)==8000);
    assert(agent_pcm_gain(INT16_MIN,100)==INT16_MIN && agent_pcm_gain(INT16_MAX,100)==INT16_MAX);
    assert(agent_pcm_gain(INT16_MIN,0)==0 && agent_pcm_gain(INT16_MAX,200)==INT16_MAX);
    unsigned char bytes[44+1200];agent_wav_header(bytes,24000,600);
    for(unsigned i=0;i<600;++i) {int16_t value=(int16_t)(i*93-27000);bytes[44+i*2]=(uint8_t)value;bytes[45+i*2]=(uint8_t)((uint16_t)value>>8);}
    for(unsigned split=1;split<70;++split) {
        sink_t out={0};agent_pcm_sink_t sink={open_pcm,write_pcm,&out};agent_wav_t w;agent_wav_init(&w,&sink);
        for(unsigned at=0;at<sizeof(bytes);) {unsigned n=split;if(n>sizeof(bytes)-at)n=(unsigned)sizeof(bytes)-at;assert(!agent_wav_feed(&w,(const char *)bytes+at,n));at+=n;}
        assert(!agent_wav_finish(&w));assert(out.opens==1 && out.count==600);
        for(unsigned i=0;i<600;++i)assert(out.data[i]==(int16_t)(i*93-27000));
    }
    sink_t out={0};agent_pcm_sink_t sink={open_pcm,write_pcm,&out};agent_wav_t w;
    agent_wav_init(&w,&sink);assert(!agent_wav_feed(&w,(char *)bytes,sizeof(bytes)-1));assert(agent_wav_finish(&w)==AGENT_ERR_PROTOCOL);
    /* Measured provider header declares an odd near-2-GiB data chunk. HTTP EOF
     * supplies the actual boundary; an incomplete 16-bit frame must still fail. */
    bytes[4]=0xbf;bytes[5]=bytes[6]=0xff;bytes[7]=0x7f;
    bytes[40]=0x9b;bytes[41]=bytes[42]=0xff;bytes[43]=0x7f;
    memset(&out,0,sizeof(out));agent_wav_init(&w,&sink);
    assert(!agent_wav_feed(&w,(char *)bytes,sizeof(bytes)));assert(!agent_wav_finish(&w));assert(out.count==600);
    memset(&out,0,sizeof(out));agent_wav_init(&w,&sink);
    assert(!agent_wav_feed(&w,(char *)bytes,sizeof(bytes)-1));assert(agent_wav_finish(&w)==AGENT_ERR_PROTOCOL);
    bytes[20]=3;agent_wav_init(&w,&sink);assert(agent_wav_feed(&w,(char *)bytes,sizeof(bytes))==AGENT_ERR_PROTOCOL);bytes[20]=1;
    memset(&out,0,sizeof(out));out.fail=true;agent_wav_init(&w,&sink);assert(agent_wav_feed(&w,(char *)bytes,sizeof(bytes))==AGENT_ERR_CANCELLED);
}
static void transcript_checks(void)
{
    char out[128];const char *json="{\"transcripts\":[{\"text\":\"你好，小言\"},{\"text\":\"请开灯\"}]}";
    assert(!agent_speech_transcript(json,strlen(json),out,sizeof(out)));assert(!strcmp(out,"你好，小言 请开灯"));
    assert(agent_speech_transcript(json,strlen(json),out,4)==AGENT_ERR_LIMIT);
    assert(agent_speech_transcript("{}",2,out,sizeof(out))==AGENT_ERR_PROTOCOL);
    assert(agent_speech_transcript("{\"transcripts\":[]}",18,out,sizeof(out))==AGENT_ERR_NOT_FOUND);
    assert(!agent_vocalign_asr.streaming && !agent_vocalign_asr.begin && !agent_vocalign_asr.feed_pcm);
    assert(!agent_vocalign_tts.streaming_text && !agent_vocalign_tts.begin && !agent_vocalign_tts.feed_text);
}
typedef struct {
    unsigned calls,post,step,poll_faults;
    agent_err_t fail,submit_fail;
    bool pending,failed_job,bad_result_url;
    char body[4096];size_t bytes;
} server_t;
static uint64_t ticks;
static uint64_t now(void *ctx) { (void)ctx;return ticks; }
static void wait_ms(unsigned n) { ticks+=n; }
static agent_err_t read_clip(void *ctx,size_t offset,int16_t *pcm,size_t n)
{ (void)ctx;for(size_t i=0;i<n;++i)pcm[i]=(int16_t)(offset+i);return AGENT_OK; }
static agent_err_t save_body(void *ctx,const char *p,size_t n)
{ server_t *s=ctx;assert(s->bytes+n<sizeof(s->body));memcpy(s->body+s->bytes,p,n);s->bytes+=n;return AGENT_OK; }
static agent_err_t perform(void *ctx,const agent_http_request_t *r,agent_http_feed_fn feed,void *dst)
{
    server_t *s=ctx;++s->calls;if(!strcmp(r->method,"POST"))++s->post;
    assert(r->reuse_connection==(r->target==AGENT_HTTP_VOCALIGN));
    if(s->fail)return s->fail;
    const char *response="";
    if(s->step==3) {
        assert(!strcmp(r->method,"GET") && strstr(r->path,"task_123"));
        if(s->poll_faults) {--s->poll_faults;return AGENT_ERR_RATE;}
        if(s->pending || s->failed_job) {
            response=s->pending?"{\"status\":\"in_progress\"}":"{\"status\":\"failed\"}";
            return feed(dst,response,strlen(response));
        }
    }
    ++s->step;
    if(s->step==1) {
        assert(r->target==AGENT_HTTP_VOCALIGN);
        response="{\"upload_host\":\"https://object.invalid/\",\"upload_dir\":\"test\",\"max_file_size_mb\":1,\"capacity_limit_mb\":1,\"oss_access_key_id\":\"a\",\"signature\":\"b\",\"policy\":\"c\",\"x_oss_object_acl\":\"private\",\"x_oss_forbid_overwrite\":\"true\"}";
    } else if(s->step==2) {
        assert(r->target==AGENT_HTTP_PUBLIC && r->produce && r->content_type);
        assert(!agent_http_write_body(r,save_body,s));assert(s->bytes==r->length);
        assert(strstr(s->body,"name=\"file\""));
    } else if(s->step==3) {
        assert(r->target==AGENT_HTTP_VOCALIGN && !strcmp(r->method,"POST"));
        if(s->submit_fail)return s->submit_fail;
        assert(strstr(r->body,"\"duration\":1"));response="{\"id\":\"task_123\",\"status\":\"queued\"}";
    } else if(s->step==4) response="{\"status\":\"in_progress\"}";
    else if(s->step==5) response=s->bad_result_url?
        "{\"status\":\"completed\",\"transcription_url\":\"http://result.invalid/file\"}":
        "{\"status\":\"completed\",\"transcription_url\":\"https://result.invalid/file\"}";
    else {assert(s->step==6 && r->target==AGENT_HTTP_PUBLIC);response="{\"transcripts\":[{\"text\":\"开灯\"}]}";}
    return feed(dst,response,strlen(response));
}
static void protocol_checks(void)
{
    server_t server={0};agent_transport_ops_t transport={perform,&server};char scratch[4096],out[128];atomic_bool cancel=false;
    agent_speech_t s={.transport=&transport,.scratch=scratch,.capacity=sizeof(scratch),.cancelled=&cancel,.now_ms=now,.wait_ms=wait_ms,.nonce=42};
    agent_speech_input_t in={.samples=512,.read=read_clip};
    assert(!agent_vocalign_asr.transcribe(&s,&in,out,sizeof(out)));assert(!strcmp(out,"开灯"));assert(server.calls==6 && server.post==2);
    server=(server_t){.fail=AGENT_ERR_AUTH};assert(agent_vocalign_asr.transcribe(&s,&in,out,sizeof(out))==AGENT_ERR_AUTH);assert(server.calls==1);
    server=(server_t){0};atomic_store(&cancel,true);
    assert(agent_vocalign_asr.transcribe(&s,&in,out,sizeof(out))==AGENT_ERR_CANCELLED && server.calls==0);
    atomic_store(&cancel,false);
    server=(server_t){.submit_fail=AGENT_ERR_TIMEOUT};
    assert(agent_vocalign_asr.transcribe(&s,&in,out,sizeof(out))==AGENT_ERR_TIMEOUT);
    assert(server.calls==3 && server.post==2); /* Never repeat an uncertain paid submission. */
    server=(server_t){.poll_faults=3};
    assert(!agent_vocalign_asr.transcribe(&s,&in,out,sizeof(out)) && server.post==2);
    server=(server_t){.poll_faults=4};
    assert(agent_vocalign_asr.transcribe(&s,&in,out,sizeof(out))==AGENT_ERR_RATE && server.post==2);
    server=(server_t){.failed_job=true};
    assert(agent_vocalign_asr.transcribe(&s,&in,out,sizeof(out))==AGENT_ERR_SERVER);
    server=(server_t){.bad_result_url=true};
    assert(agent_vocalign_asr.transcribe(&s,&in,out,sizeof(out))==AGENT_ERR_PROTOCOL && server.step==5);
    server=(server_t){.pending=true};ticks=0;
    assert(agent_vocalign_asr.transcribe(&s,&in,out,sizeof(out))==AGENT_ERR_TIMEOUT);
    assert(ticks>=180000 && ticks<=182000 && server.post==2);
}
typedef struct { char bytes[5000];size_t used,calls,capacity;bool fail;atomic_bool *cancel; } stream_out_t;
static agent_err_t fragments(void *ctx,agent_write_fn write,void *dst)
{
    (void)ctx;
    for(size_t i=0;i<5000;++i) {
        char c=(char)(i%251);agent_err_t e=write(dst,&c,1);if(e) return e;
    }
    return AGENT_OK;
}
static agent_err_t buffered_sink(void *ctx,const char *data,size_t n)
{
    stream_out_t *o=ctx;++o->calls;
    assert(n && n<=o->capacity && o->used+n<=sizeof(o->bytes));
    if(o->fail)return AGENT_ERR_NETWORK;
    memcpy(o->bytes+o->used,data,n);o->used+=n;
    if(o->cancel)atomic_store(o->cancel,true);
    return AGENT_OK;
}
static void buffered_checks(void)
{
    char scratch[1024];atomic_bool cancel=false;
    agent_http_request_t r={.produce=fragments,.length=5000,.cancelled=&cancel};
    const size_t sizes[]={1,17,128,1024};
    for(size_t j=0;j<sizeof(sizes)/sizeof(*sizes);++j) {
        stream_out_t out={.capacity=sizes[j]};
        assert(!agent_http_write_buffered(&r,buffered_sink,&out,scratch,sizes[j]));
        assert(out.used==5000 && out.calls==(5000+sizes[j]-1)/sizes[j]);
        for(size_t i=0;i<5000;++i)assert(out.bytes[i]==(char)(i%251));
    }
    stream_out_t out={.capacity=1024,.fail=true};
    assert(agent_http_write_buffered(&r,buffered_sink,&out,scratch,1024)==AGENT_ERR_NETWORK && out.calls==1);
    out=(stream_out_t){.capacity=1024,.cancel=&cancel};
    assert(agent_http_write_buffered(&r,buffered_sink,&out,scratch,1024)==AGENT_ERR_CANCELLED && out.calls==1);
    atomic_store(&cancel,false);r.length=5001;out=(stream_out_t){.capacity=1024};
    assert(agent_http_write_buffered(&r,buffered_sink,&out,scratch,1024)==AGENT_ERR_PROTOCOL && out.used==4096);
    r.length=4999;out=(stream_out_t){.capacity=1024};
    assert(agent_http_write_buffered(&r,buffered_sink,&out,scratch,1024)==AGENT_ERR_LIMIT);
}
static void intent_checks(void)
{
    const char *unfinished[]={"请把灯调成蓝色。不对，不。","不对，改成", "不是，换成……",
        "把燈轉藍色，唔係，唔好。", "唔系，改做\r\n", "不對，不要！", "不是？"};
    const char *other[]={"", "你好", "请把灯调成蓝色，不对，改成绿色。", "唔係，轉綠色。",
        "不要", "这个答案不对。", "对不对？", "请解释‘不对，不’。", "请读出“不对，不要”。",
        "不是蓝色，是绿色。", "唔好轉藍色。"};
    for(size_t i=0;i<sizeof(unfinished)/sizeof(*unfinished);++i)assert(agent_speech_unfinished_repair(unfinished[i]));
    for(size_t i=0;i<sizeof(other)/sizeof(*other);++i)assert(!agent_speech_unfinished_repair(other[i]));
    /* A complete short negation can also match: choose clarification over
     * guessing an action, without claiming a completeness classifier. */
    assert(agent_speech_unfinished_repair("不是，先不要。"));
    assert(!agent_speech_unfinished_repair(NULL));
}
int main(void) { wav_checks();transcript_checks();protocol_checks();buffered_checks();intent_checks();puts("cloud speech checks passed");return 0; }
