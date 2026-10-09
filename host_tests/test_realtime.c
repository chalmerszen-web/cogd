#include "realtime.h"
#include "draft.h"
#include "ws_frame.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FORMAT16 "\"audio\":{\"output\":{\"format\":{\"type\":\"pcm\",\"sample_rate\":16000}}}"

typedef struct {
    char scratch[8192],sent[20][2048];
    agent_realtime_t q;
    agent_speech_t speech;
    agent_ws_ops_t ws;
    atomic_bool cancelled;
    uint64_t ticks;
    unsigned opens,closes,sends,attempts,busy,events,tool_events,done_events,pcm_opens,reads;
    unsigned incoming_at,incoming_count,block_after_sends;
    const char *incoming[8];
    const char *configuration;
    size_t samples,partial,split;
    int16_t pcm[240000];
    bool handshake,updated,never_ready,cancel_on_write;
    agent_err_t send_error,read_error,callback_error,sink_error;
} mock_t;
static uint64_t clock_ms(void *p) { return ((mock_t *)p)->ticks; }
static agent_err_t open_ws(void *p,const atomic_bool *cancel)
{ mock_t *m=p;assert(!atomic_load(cancel));++m->opens;return AGENT_OK; }
static void close_ws(void *p) { ++((mock_t *)p)->closes; }
static agent_err_t send_ws(void *p,bool binary,char *data,size_t length,const atomic_bool *cancel)
{
    mock_t *m=p;assert(!binary && length<2048 && !atomic_load(cancel));++m->attempts;
    if(m->busy) {--m->busy;return AGENT_ERR_BUSY;}
    if(m->block_after_sends && m->sends>=m->block_after_sends)return AGENT_ERR_BUSY;
    if(m->send_error)return m->send_error;
    assert(m->sends<20);memcpy(m->sent[m->sends],data,length);m->sent[m->sends++][length]=0;
    cJSON *json=agent_json_parse(data,length);assert(json);
    if(!strcmp(agent_json_string(json,"type"),"session.update"))m->updated=true;
    cJSON_Delete(json);return AGENT_OK;
}
static agent_err_t read_ws(void *p,char *data,size_t cap,agent_ws_chunk_t *part,unsigned ms,const atomic_bool *cancel)
{
    mock_t *m=p;m->ticks+=ms;++m->reads;assert(!atomic_load(cancel));
    if(m->read_error)return m->read_error;
    const char *text=NULL;
    if(!m->handshake)text="{\"type\":\"session.created\",\"session\":{\"id\":\"one\"}}";
    else if(m->updated && !m->q.ready && !m->never_ready)text=m->configuration?m->configuration:"{\"type\":\"session.updated\",\"session\":{" FORMAT16 "}}";
    else if(m->incoming_at<m->incoming_count)text=m->incoming[m->incoming_at];
    if(!text)return AGENT_OK;
    size_t total=strlen(text),n=total-m->partial;if(n>cap)n=cap;if(n>m->split)n=m->split;
    *part=(agent_ws_chunk_t){n,1,m->partial==0,m->partial+n==total};
    memcpy(data,text+m->partial,n);m->partial+=n;
    if(m->partial==total) {
        if(m->handshake && m->q.ready)++m->incoming_at;
        m->handshake=true;m->partial=0;
    }
    return AGENT_OK;
}
static agent_err_t sink_open(void *p,unsigned rate)
{ mock_t *m=p;assert(rate==16000);++m->pcm_opens;return AGENT_OK; }
static agent_err_t sink_write(void *p,const int16_t *samples,size_t count)
{
    mock_t *m=p;assert(count<=128 && m->samples+count<=240000);
    if(m->sink_error)return m->sink_error;
    memcpy(m->pcm+m->samples,samples,count*2);m->samples+=count;
    if(m->cancel_on_write)atomic_store(&m->cancelled,true);
    return AGENT_OK;
}
static agent_err_t event(void *p,const cJSON *json)
{
    mock_t *m=p;++m->events;const char *type=agent_json_string(json,"type");assert(type);
    assert(agent_realtime_create_response(&m->q)==AGENT_ERR_BUSY);
    assert(agent_realtime_update(&m->q,"nested","[]")==AGENT_ERR_BUSY);
    assert(agent_realtime_commit_input(&m->q)==AGENT_ERR_BUSY);
#if AGENT_RT_TEXT_REUSE
    assert(agent_realtime_next_text_turn(&m->q)==AGENT_ERR_BUSY);
#endif
    if(!strcmp(type,"response.audio.delta"))assert(!strcmp(agent_json_string(json,"delta"),""));
    if(!strcmp(type,"response.function_call_arguments.done"))++m->tool_events;
    if(!strcmp(type,"response.done"))++m->done_events;
    return m->callback_error;
}
static mock_t *setup(bool ready)
{
    mock_t *m=calloc(1,sizeof(*m));assert(m);atomic_init(&m->cancelled,false);m->split=37;
    m->speech=(agent_speech_t){.scratch=m->scratch,.capacity=sizeof(m->scratch),.cancelled=&m->cancelled,.now_ms=clock_ms,.clock_ctx=m};
    m->ws=(agent_ws_ops_t){open_ws,send_ws,read_ws,close_ws,m};
    agent_pcm_sink_t sink={sink_open,sink_write,m};
    agent_realtime_init(&m->q,&m->speech,&m->ws,&sink,event,m);
    m->q.deadline=120000;
    if(ready)m->q.created=m->q.ready=m->q.opened=m->handshake=true;
    return m;
}
static agent_err_t fragment(mock_t *m,const char *json,size_t split,uint32_t seed)
{
    size_t length=strlen(json),offset=0;
    while(offset<length) {
        seed=seed*1664525u+1013904223u;size_t n=seed%split+1;if(n>length-offset)n=length-offset;
        /* Alternate WebSocket continuation frames and chunks of one frame. */
        agent_ws_chunk_t part={n,offset?0:1,offset==0 || (seed&1),offset+n==length};
        agent_err_t e=agent_realtime_receive(&m->q,json+offset,&part);if(e)return e;offset+=n;
    }
    return AGENT_OK;
}
static agent_err_t whole(mock_t *m,const char *json) { return fragment(m,json,strlen(json),7); }
static void started(mock_t *m)
{ m->q.pending=true;assert(!whole(m,"{\"type\":\"response.created\",\"response\":{\"id\":\"r1\"}}")); }
static const char *finished="{\"type\":\"response.done\",\"response\":{\"id\":\"r1\",\"status\":\"completed\",\"output\":[]}}";
static char *audio_json(const char *id,size_t bytes)
{
    static const char abc[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t cap=256+((bytes+2)/3)*4;char *text=malloc(cap);assert(text);
    int head=snprintf(text,cap,"{\"event_id\":\"event_1\",\"type\":\"response.audio.delta\",\"response_id\":\"%s\",\"output_index\":0,\"delta\":\"",id);assert(head>0);
    size_t used=(size_t)head;
    for(size_t i=0;i<bytes;i+=3) {
        unsigned v=0,n=bytes-i<3?(unsigned)(bytes-i):3;
        for(unsigned j=0;j<3;++j)v=(v<<8)|(j<n?(unsigned)((i+j)*73+19)&255:0);
        text[used++]=abc[v>>18];text[used++]=abc[(v>>12)&63];text[used++]=n>1?abc[(v>>6)&63]:'=';text[used++]=n>2?abc[v&63]:'=';
    }
    strcpy(text+used,"\",\"content_index\":0}");return text;
}
static void streaming(void)
{
    char *json=audio_json("r1",19200);assert(strlen(json)>25600);
    for(unsigned split=1;split<=512;split=split==1?7:split*2+1) {
        mock_t *m=setup(true);started(m);assert(!fragment(m,json,split,split*431));
        assert(m->samples==9600 && m->q.used<256 && m->pcm_opens==1);
        for(size_t i=0;i<m->samples;++i)assert(m->pcm[i]==(int16_t)((((2*i)*73+19)&255)|((((2*i+1)*73+19)&255)<<8)));
        assert(!whole(m,"{\"type\":\"response.audio.done\",\"response_id\":\"r1\"}"));
        assert(!whole(m,finished));assert(m->q.done && !m->q.active && !m->q.error);
        started(m);assert(!whole(m,finished));assert(m->done_events==2);free(m);
    }
    free(json);
    mock_t *m=setup(true);started(m);
    /* Odd PCM bytes can cross delta events, with canonical padding per event. */
    assert(!whole(m,"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"Eg==\"}"));
    assert(!m->samples && m->q.odd);
    assert(!whole(m,"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"NA==\"}"));
    assert(m->samples==1 && m->pcm[0]==0x3412);
    assert(!whole(m,"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"\"}"));
    assert(!whole(m,finished));free(m);
    m=setup(true);started(m);
    assert(!fragment(m,"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"\\u0045jQ=\"}",1,1));
    assert(m->samples==1 && m->pcm[0]==0x3412);free(m);
}
static void metadata(void)
{
    mock_t *m=setup(true);started(m);
    const char *text="{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"r1\",\"delta\":\"你好，小言。\\\"\\n\"}";
    assert(!fragment(m,text,1,3));assert(!m->samples);
    const char *tool="{\"type\":\"response.function_call_arguments.done\",\"response_id\":\"r1\",\"name\":\"device_light_set_rgb\",\"call_id\":\"call_1\",\"arguments\":\"{\\\"r\\\":0,\\\"g\\\":0,\\\"b\\\":255}\"}";
    assert(!fragment(m,tool,3,7));assert(m->tool_events==1 && !m->sends && !m->samples);
    assert(!whole(m,finished));assert(!agent_realtime_tool_result(&m->q,"call_1","{\"ok\":true,\"host_mock\":true}"));
    assert(!agent_realtime_create_response(&m->q));assert(m->sends==2 && !m->q.done);
    cJSON *sent=agent_json_parse(m->sent[0],strlen(m->sent[0]));
    const cJSON *item=cJSON_GetObjectItemCaseSensitive(sent,"item");assert(!strcmp(agent_json_string(item,"call_id"),"call_1"));
    assert(!strcmp(agent_json_string(item,"output"),"{\"ok\":true,\"host_mock\":true}"));cJSON_Delete(sent);
    started(m);assert(!whole(m,finished));free(m);
}
static void errors(void)
{
    const struct { const char *json;agent_err_t error; } cases[]={
        {"{\"delta\":\"AAAA\",\"type\":\"response.audio.delta\"}",AGENT_ERR_PROTOCOL},
        {"{\"type\":\"response.audio.delta\",\"response_id\":\"wrong\",\"delta\":\"AAAA\"}",AGENT_ERR_PROTOCOL},
        {"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"AAA\"}",AGENT_ERR_PROTOCOL},
        {"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"AB==\"}",AGENT_ERR_PROTOCOL},
        {"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"AA=A\"}",AGENT_ERR_PROTOCOL},
        {"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"AA==AAAA\"}",AGENT_ERR_PROTOCOL},
        {"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"!!!!\"}",AGENT_ERR_PROTOCOL},
        {"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"\\uZZZZ\"}",AGENT_ERR_JSON},
        {"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"AAAA",AGENT_ERR_JSON},
        {"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"AAAA\",oops}",AGENT_ERR_JSON},
        {"{\"type\":\"response.audio.delta\",\"type\":\"response.audio.delta\",\"delta\":\"AAAA\"}",AGENT_ERR_JSON},
        {"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":42}",AGENT_ERR_PROTOCOL},
        {"{\"type\":\"response.done\",\"response\":{\"id\":\"r1\",\"status\":\"failed\"}}",AGENT_ERR_SERVER},
        {"{\"type\":\"error\",\"error\":{\"code\":\"invalid_request_error\"}}",AGENT_ERR_SERVER},
        {"{\"type\":\"error\",\"error\":{\"code\":\"invalid_api_key\"}}",AGENT_ERR_AUTH},
        {"{\"type\":\"error\",\"error\":{\"code\":\"rate_limit_exceeded\"}}",AGENT_ERR_RATE},
        {"{\"type\":\"response.audio_transcript.delta\",\"delta\":\"\xc0\xaf\"}",AGENT_ERR_JSON},
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        mock_t *m=setup(true);started(m);agent_err_t e=fragment(m,cases[i].json,7,11);
        if(e!=cases[i].error)fprintf(stderr,"case %zu expected %d actual %d\n",i,cases[i].error,e);
        assert(e==cases[i].error);assert(m->q.error==e && m->closes==1);
        assert(whole(m,finished)==e);assert(!m->sends);free(m);
    }
    mock_t *m=setup(true);started(m);
    assert(!whole(m,"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"Eg==\"}"));
    assert(whole(m,finished)==AGENT_ERR_PROTOCOL);free(m);
    m=setup(true);started(m);m->q.pcm_bytes=AGENT_RT_PCM_MAX;
    assert(whole(m,"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"EjQ=\"}")==AGENT_ERR_LIMIT);free(m);
    m=setup(true);started(m);char *large=audio_json("r1",192002);
    assert(fragment(m,large,511,7)==AGENT_ERR_LIMIT);free(large);free(m);
    m=setup(true);char *big=malloc(7000);assert(big);memset(big,'x',6999);big[0]='{';big[6999]=0;
    assert(fragment(m,big,128,5)==AGENT_ERR_LIMIT);free(big);free(m);
    m=setup(true);started(m);agent_ws_chunk_t empty={0,1,true,true};
    assert(agent_realtime_receive(&m->q,NULL,&empty)==AGENT_ERR_JSON);free(m);
    m=setup(true);agent_ws_chunk_t binary={1,2,true,true};
    assert(agent_realtime_receive(&m->q,"a",&binary)==AGENT_ERR_PROTOCOL);free(m);
    m=setup(true);started(m);m->callback_error=AGENT_ERR_TOOL;assert(whole(m,finished)==AGENT_ERR_TOOL);free(m);
    m=setup(true);started(m);m->sink_error=AGENT_ERR_CANCELLED;
    assert(whole(m,"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"EjQ=\"}")==AGENT_ERR_CANCELLED);free(m);
    m=setup(true);started(m);m->cancel_on_write=true;char *cancel_audio=audio_json("r1",19200);
    assert(fragment(m,cancel_audio,511,1)==AGENT_ERR_CANCELLED && m->samples<=128 && m->closes==1);
    free(cancel_audio);free(m);
    m=setup(true);started(m);atomic_store(&m->cancelled,true);assert(whole(m,finished)==AGENT_ERR_CANCELLED);free(m);
    m=setup(true);m->ticks=120000;assert(agent_realtime_poll(&m->q,50)==AGENT_ERR_TIMEOUT && m->closes==1);free(m);
    m=setup(true);m->ticks=120000;assert(agent_realtime_create_response(&m->q)==AGENT_ERR_TIMEOUT && !m->sends);free(m);
    m=setup(true);agent_realtime_cancel(&m->q);agent_realtime_cancel(&m->q);assert(m->closes==1 && !m->sends);free(m);
    m=setup(true);started(m);agent_ws_chunk_t partial={4,1,true,false};
    assert(!agent_realtime_receive(&m->q,"{\"ty",&partial));m->read_error=AGENT_ERR_NETWORK;
    assert(agent_realtime_poll(&m->q,50)==AGENT_ERR_NETWORK && m->closes==1 && !m->q.done);free(m);
    m=setup(true);started(m);m->ticks=45000;assert(whole(m,finished)==AGENT_ERR_TIMEOUT);free(m);
}
static void sending(void)
{
    mock_t *m=setup(false);m->busy=3;
    assert(!agent_realtime_begin(&m->q,"你好，简短回答。","[]",false));
    assert(m->q.ready && m->sends==1 && m->attempts==4 && m->opens==1);
    cJSON *config=agent_json_parse(m->sent[0],strlen(m->sent[0]));
    const cJSON *session=cJSON_GetObjectItemCaseSensitive(config,"session");
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(session,"turn_detection")));
    assert(!strcmp(agent_json_string(session,"voice"),"Tina"));
    const cJSON *format=cJSON_GetObjectItemCaseSensitive(session,"audio");
    format=cJSON_GetObjectItemCaseSensitive(format,"output");
    format=cJSON_GetObjectItemCaseSensitive(format,"format");
    assert(!strcmp(agent_json_string(format,"type"),"pcm"));
    assert(cJSON_GetObjectItemCaseSensitive(format,"sample_rate")->valueint==16000);
    assert(!cJSON_GetObjectItemCaseSensitive(session,"output_audio_format"));cJSON_Delete(config);
    int16_t samples[513];for(unsigned i=0;i<513;++i)samples[i]=(int16_t)(i*431-32700);
    assert(!agent_realtime_feed_pcm(&m->q,samples,513));assert(m->sends==3 && m->q.input_samples==513);
    /* Round-trip encoded PCM through the independent receive path. */
    mock_t *decoder=setup(true);started(decoder);
    for(unsigned i=1;i<=2;++i) {
        cJSON *packet=agent_json_parse(m->sent[i],strlen(m->sent[i]));const char *base64=agent_json_string(packet,"audio");assert(base64);
        char reply[1900];snprintf(reply,sizeof(reply),"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"%s\"}",base64);
        assert(!fragment(decoder,reply,19,3));cJSON_Delete(packet);
    }
    assert(decoder->samples==513 && !memcmp(decoder->pcm,samples,sizeof(samples)));free(decoder);
    assert(!agent_realtime_commit(&m->q));assert(m->sends==5 && m->q.pending && !m->q.input_samples);
    assert(agent_realtime_create_response(&m->q)==AGENT_ERR_BUSY);
    started(m);assert(!whole(m,finished));
    m->send_error=AGENT_ERR_NETWORK;unsigned attempts=m->attempts;
    assert(agent_realtime_create_response(&m->q)==AGENT_ERR_NETWORK);
    assert(m->attempts==attempts+1 && m->closes==1);free(m);
    m=setup(false);assert(!agent_realtime_begin(&m->q,"简短。","[]",true));
    config=agent_json_parse(m->sent[0],strlen(m->sent[0]));session=cJSON_GetObjectItemCaseSensitive(config,"session");
    const cJSON *vad=cJSON_GetObjectItemCaseSensitive(session,"turn_detection");uint64_t silence=0;
    assert(!strcmp(agent_json_string(vad,"type"),"server_vad"));
    assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(vad,"silence_duration_ms"),1000,&silence) && silence==800);cJSON_Delete(config);
    assert(agent_realtime_commit(&m->q)==AGENT_ERR_PROTOCOL);free(m);
    m=setup(false);m->never_ready=true;assert(agent_realtime_begin(&m->q,"x","[]",false)==AGENT_ERR_TIMEOUT);assert(m->opens==1 && m->sends==1 && m->closes==1);free(m);
    m=setup(false);atomic_store(&m->cancelled,true);assert(agent_realtime_begin(&m->q,"x","[]",false)==AGENT_ERR_CANCELLED);assert(!m->opens);free(m);
    m=setup(false);char too_large[2048];memset(too_large,'x',2047);too_large[2047]=0;
    assert(agent_realtime_begin(&m->q,too_large,"[]",false)==AGENT_ERR_LIMIT);assert(!m->opens);free(m);
    m=setup(true);m->q.input_samples=AGENT_RT_INPUT_MAX;assert(agent_realtime_feed_pcm(&m->q,samples,1)==AGENT_ERR_LIMIT);assert(!m->sends);free(m);
}
static const char *speech_started="{\"type\":\"input_audio_buffer.speech_started\",\"audio_start_ms\":0,\"item_id\":\"input_1\"}";
static const char *speech_stopped="{\"type\":\"input_audio_buffer.speech_stopped\",\"audio_end_ms\":32,\"item_id\":\"input_1\"}";
static const char *response_created="{\"type\":\"response.created\",\"response\":{\"id\":\"r1\"}}";
static mock_t *auto_input(void)
{
    mock_t *m=setup(true);m->q.auto_vad=true;int16_t pcm[32]={0};size_t accepted=99;
    assert(!agent_realtime_feed_pcm_some(&m->q,pcm,32,&accepted));
    assert(accepted==32 && m->q.input_samples==32 && m->sends==1);return m;
}
static void auto_vad(void)
{
    mock_t *m=auto_input();assert(!fragment(m,speech_started,1,4));
    assert(m->q.input_started && !m->q.input_ended && !m->q.input_paused);
    size_t length=strlen(speech_stopped);
    agent_ws_chunk_t part={length-1,1,true,false};
    assert(!agent_realtime_receive(&m->q,speech_stopped,&part));
    assert(!m->q.input_ended && m->q.message && agent_realtime_resume(&m->q)==AGENT_ERR_PROTOCOL);
    part=(agent_ws_chunk_t){1,0,true,true};assert(!agent_realtime_receive(&m->q,speech_stopped+length-1,&part));
    assert(m->q.input_ended && m->q.input_paused && !m->q.message && m->q.input_end_ms==32);
    unsigned reads=m->reads,sends=m->sends,events=m->events;size_t accepted=99;
    m->incoming[0]="{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"input_1\"}";
    m->incoming[1]=response_created;m->incoming[2]="{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"EjQ=\"}";
    m->incoming[3]=finished;m->incoming_count=4;
    for(unsigned i=0;i<3;++i)assert(!agent_realtime_poll(&m->q,50));
    assert(m->reads==reads && m->events==events && !m->samples && !m->pcm_opens);
    int16_t pcm[1024]={0};assert(agent_realtime_feed_pcm_some(&m->q,pcm,512,&accepted)==AGENT_ERR_BUSY);
    assert(!accepted && m->sends==sends && m->q.input_samples==32 && !m->q.error);
    assert(agent_realtime_create_response(&m->q)==AGENT_ERR_PROTOCOL);
    assert(agent_realtime_commit(&m->q)==AGENT_ERR_PROTOCOL);
    assert(whole(m,response_created)==AGENT_ERR_BUSY && !m->q.error && m->events==events);
    assert(!agent_realtime_resume(&m->q) && m->q.pending && !m->q.input_paused && m->sends==sends);
    while(!m->q.done)assert(!agent_realtime_poll(&m->q,20));
    assert(m->samples==1 && m->pcm[0]==0x3412 && m->q.input_samples==32 && m->sends==sends);
    assert(agent_realtime_feed_pcm_some(&m->q,pcm,1,&accepted)==AGENT_ERR_BUSY && !accepted);
    /* A tool continuation is explicitly sent only after the automatic first
     * response completes; resume itself did not create a duplicate response. */
    assert(!agent_realtime_tool_result(&m->q,"call_1","{\"ok\":true}"));
    assert(!agent_realtime_create_response(&m->q) && m->sends==sends+2);
    assert(!whole(m,response_created));assert(!whole(m,finished));free(m);

    /* An unsent PCM frame meets server endpoint while send(BUSY) pumps input.
     * It must remain unsent, rather than retry after the endpoint callback. */
    for(unsigned fragmented=0;fragmented<2;++fragmented) {
        m=auto_input();m->split=fragmented?3:512;m->block_after_sends=m->sends;
        m->incoming[0]=speech_started;m->incoming[1]=speech_stopped;m->incoming[2]=response_created;m->incoming_count=3;
        sends=m->sends;accepted=99;
        assert(agent_realtime_feed_pcm_some(&m->q,pcm,512,&accepted)==AGENT_ERR_BUSY);
        assert(!accepted && m->sends==sends && m->q.input_samples==32 && m->q.input_ended && !m->q.error);
        assert(m->incoming_at==2 && !m->q.active && !m->samples);
        unsigned attempts=m->attempts;reads=m->reads;
        assert(agent_realtime_feed_pcm_some(&m->q,pcm,1,&accepted)==AGENT_ERR_BUSY);
        assert(m->attempts==attempts && m->reads==reads);free(m);
    }
    m=auto_input();assert(!whole(m,speech_started));m->split=9;
    m->block_after_sends=m->sends+1;m->incoming[0]=speech_stopped;m->incoming_count=1;
    sends=m->sends;
    assert(agent_realtime_feed_pcm_some(&m->q,pcm,1024,&accepted)==AGENT_ERR_BUSY);
    assert(accepted==512 && m->q.input_samples==544 && m->sends==sends+1 && m->q.input_ended && !m->q.error);free(m);

    /* A successful send remains in accepted even when a subsequent receive
     * fails. An ambiguous/failed send is never counted or automatically retried. */
    m=auto_input();m->read_error=AGENT_ERR_NETWORK;sends=m->sends;
    assert(agent_realtime_feed_pcm_some(&m->q,pcm,512,&accepted)==AGENT_ERR_NETWORK);
    assert(accepted==512 && m->sends==sends+1 && m->q.input_samples==544 && m->closes==1);free(m);
    m=auto_input();m->send_error=AGENT_ERR_NETWORK;sends=m->sends;
    assert(agent_realtime_feed_pcm_some(&m->q,pcm,512,&accepted)==AGENT_ERR_NETWORK);
    assert(!accepted && m->sends==sends && m->q.input_samples==32 && m->closes==1);free(m);

    const char *invalid_stops[]={
        "{\"type\":\"input_audio_buffer.speech_stopped\",\"audio_end_ms\":32,\"item_id\":\"wrong\"}",
        "{\"type\":\"input_audio_buffer.speech_stopped\",\"audio_end_ms\":-1,\"item_id\":\"input_1\"}",
        "{\"type\":\"input_audio_buffer.speech_stopped\",\"audio_end_ms\":0.5,\"item_id\":\"input_1\"}",
        "{\"type\":\"input_audio_buffer.speech_stopped\",\"audio_end_ms\":10001,\"item_id\":\"input_1\"}",
        "{\"type\":\"input_audio_buffer.speech_stopped\",\"item_id\":\"input_1\"}",
        "{\"type\":\"input_audio_buffer.speech_stopped\",\"audio_end_ms\":32,\"item_id\":\"input_1\",\"reason\":\"turn_invalid\"}"
    };
    for(unsigned i=0;i<sizeof(invalid_stops)/sizeof(*invalid_stops);++i) {
        m=auto_input();assert(!whole(m,speech_started));
        assert(fragment(m,invalid_stops[i],3,7)==AGENT_ERR_PROTOCOL && !m->q.input_ended && m->closes==1);free(m);
    }
    m=auto_input();assert(whole(m,speech_stopped)==AGENT_ERR_PROTOCOL && !m->samples);free(m);
    m=setup(true);m->q.auto_vad=true;assert(whole(m,speech_started)==AGENT_ERR_PROTOCOL);free(m);
    m=auto_input();assert(!whole(m,speech_started));assert(whole(m,speech_started)==AGENT_ERR_PROTOCOL);free(m);
    m=auto_input();assert(!whole(m,"{\"type\":\"input_audio_buffer.speech_started\",\"audio_start_ms\":40,\"item_id\":\"input_1\"}"));
    assert(whole(m,speech_stopped)==AGENT_ERR_PROTOCOL);free(m);

    const char *early[]={response_created,"{\"type\":\"response.text.delta\",\"delta\":\"unsolicited\"}",
        "{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"EjQ=\"}"};
    for(unsigned i=0;i<sizeof(early)/sizeof(*early);++i) {
        m=auto_input();events=m->events;assert(whole(m,early[i])==AGENT_ERR_PROTOCOL);
        assert(m->events==events && !m->samples && !m->pcm_opens);free(m);
    }
    for(unsigned mode=0;mode<3;++mode) {
        m=auto_input();assert(!whole(m,speech_started));assert(!whole(m,speech_stopped));reads=m->reads;
        if(mode==0)atomic_store(&m->cancelled,true);
        if(mode==1)m->ticks=m->q.deadline;
        if(mode==2) {
            assert(!agent_realtime_resume(&m->q));
            assert(whole(m,speech_stopped)==AGENT_ERR_PROTOCOL);free(m);continue;
        }
        assert(agent_realtime_poll(&m->q,50)==(mode==0?AGENT_ERR_CANCELLED:AGENT_ERR_TIMEOUT));
        assert(m->reads==reads && m->closes==1);free(m);
    }
    m=auto_input();assert(!whole(m,speech_started));m->callback_error=AGENT_ERR_TOOL;
    assert(whole(m,speech_stopped)==AGENT_ERR_TOOL && m->q.error==AGENT_ERR_TOOL && m->closes==1);free(m);
}
static void tool_transition(void)
{
    const char *created="{\"type\":\"response.created\",\"response\":{\"id\":\"r2\"}}";
    const char *tool="{\"type\":\"response.output_item.added\",\"response_id\":\"r2\",\"output_index\":0,\"item\":{\"type\":\"function_call\",\"name\":\"delegate_deepseek\"}}";
    mock_t *m=setup(true);started(m);
    assert(whole(m,created)==AGENT_ERR_PROTOCOL);free(m);
    for(unsigned split=1;split<=64;split*=8) {
        m=setup(true);started(m);m->q.allow_tool_transition=true;
        assert(!fragment(m,created,split,9) && m->q.tool_transition_pending && !m->q.allow_tool_transition);
        assert(!fragment(m,tool,split,7) && !m->q.tool_transition_pending);
        assert(!whole(m,"{\"type\":\"response.done\",\"response\":{\"id\":\"r2\",\"status\":\"completed\",\"output\":[]}}"));
        assert(m->q.done && !m->samples);free(m);
    }
    const char *invalid[]={
        "{\"type\":\"response.output_item.added\",\"response_id\":\"r2\",\"output_index\":0,\"item\":{\"type\":\"message\"}}",
        "{\"type\":\"response.output_item.added\",\"response_id\":\"r2\",\"output_index\":1,\"item\":{\"type\":\"function_call\"}}",
        "{\"type\":\"response.audio.delta\",\"response_id\":\"r2\",\"delta\":\"EjQ=\"}",
        "{\"type\":\"response.done\",\"response\":{\"id\":\"r2\",\"status\":\"completed\",\"output\":[]}}"
    };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        m=setup(true);started(m);m->q.allow_tool_transition=true;assert(!whole(m,created));
        assert(fragment(m,invalid[i],3,7)==AGENT_ERR_PROTOCOL && !m->samples);free(m);
    }
    m=setup(true);started(m);m->q.allow_tool_transition=true;
    assert(whole(m,response_created)==AGENT_ERR_PROTOCOL);free(m);
    for(unsigned old=0;old<2;++old) {
        m=setup(true);started(m);m->q.allow_tool_transition=true;assert(!whole(m,created));assert(!whole(m,tool));
        assert(whole(m,old?finished:response_created)==AGENT_ERR_PROTOCOL);free(m);
    }
}
static void fixture(const char *path)
{
    FILE *file=fopen(path,"rb");assert(file);char line[8192];mock_t *m=setup(true);
    m->q.created=m->q.ready=false;unsigned count=0;
    while(fgets(line,sizeof(line),file)) {
        cJSON *record=agent_json_parse(line,strlen(line));assert(record);
        cJSON *item=cJSON_GetObjectItemCaseSensitive(record,"event");assert(item);
        const char *type=agent_json_string(item,"type");char *wire=NULL;
        if(!strcmp(type,"response.created"))m->q.pending=true; /* Recorded manual response.create. */
        if(!strcmp(type,"response.audio.delta")) {
            uint64_t bytes;assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(record,"audio_pcm_bytes"),192000,&bytes));
            /* Probe redacted the original base64 and its key position. Rebuild
             * a valid payload; this tests schemas, NOT the original wire order. */
            wire=audio_json(agent_json_string(item,"response_id"),(size_t)bytes);
        } else wire=cJSON_PrintUnformatted(item);
        assert(wire);agent_err_t e=fragment(m,wire,127,count*37+1);
        if(e)fprintf(stderr,"fixture event %u %s: %d\n",count,type,e);
        assert(!e);free(wire);cJSON_Delete(record);++count;
    }
    assert(count==98 && m->done_events==3 && m->tool_events==1 && !m->sends && m->samples==215040);
    printf("actual probe metadata replay: %u events, %zu samples, no automatic tool sends\n",count,m->samples);
    fclose(file);free(m);
}
static void transcription_failure(void)
{
    const char *codes[]={"transcription_failed","invalid_api_key","rate_limit_exceeded","permission_denied"};
    const agent_err_t expected[]={AGENT_ERR_SERVER,AGENT_ERR_AUTH,AGENT_ERR_RATE,AGENT_ERR_FORBIDDEN};
    for(unsigned i=0;i<4;++i)for(unsigned split=1;split<=64;split*=8) {
        mock_t *m=auto_input();assert(!whole(m,speech_started));
        char text[256];snprintf(text,sizeof(text),
            "{\"type\":\"conversation.item.input_audio_transcription.failed\",\"item_id\":\"input_1\",\"content_index\":0,\"error\":{\"code\":\"%s\"}}",codes[i]);
        unsigned events=m->events,sends=m->sends;m->callback_error=AGENT_ERR_TOOL;
        assert(fragment(m,text,split,42)==expected[i]);
        assert(m->q.error==expected[i] && m->events==events+1 && m->closes==1 && !m->samples);
        /* Observer errors must not overwrite the provider's terminal failure.
         * Subsequent operations return it without another send or retry. */
        assert(agent_realtime_poll(&m->q,20)==expected[i] && m->sends==sends);
        agent_realtime_cancel(&m->q);assert(m->closes==1);free(m);
    }
}
static void absolute_cap(void)
{
    mock_t *m=setup(true);m->q.deadline_cap=2500;
    m->ticks=2000;started(m);
    assert(m->q.deadline==47000 && m->q.deadline_cap==2500);
    m->ticks=2499;
    assert(agent_realtime_poll(&m->q,50)==AGENT_ERR_TIMEOUT);
    assert(m->ticks==2500 && m->closes==1 && !m->samples);free(m);

    /* response.create must obey the same cap while the socket is not writable. */
    m=setup(true);m->q.deadline_cap=100;m->q.input_samples=1;m->block_after_sends=1;
    assert(agent_realtime_commit(&m->q)==AGENT_ERR_TIMEOUT);
    assert(m->ticks==100 && m->sends==1 && m->closes==1 && !m->samples);free(m);

    m=setup(true);m->q.deadline_cap=100;started(m);m->ticks=100;
    assert(whole(m,"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"EjR4Vg==\"}")==AGENT_ERR_TIMEOUT);
    assert(!m->samples && m->closes==1);free(m);

    /* The owner can remove the startup cap after starting a real stream. */
    m=setup(true);m->q.deadline_cap=100;started(m);m->q.deadline_cap=0;m->ticks=5000;
    assert(!whole(m,"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"EjR4Vg==\"}"));
    assert(m->samples==2 && !m->closes);free(m);

    m=setup(true);m->q.deadline_cap=100;m->ticks=100;atomic_store(&m->cancelled,true);
    assert(agent_realtime_poll(&m->q,50)==AGENT_ERR_CANCELLED && m->closes==1);free(m);
}
static void reusable_turn(mock_t *m,unsigned turn)
{
    int16_t pcm[2]={1,2};char wire[256];
    assert(!agent_realtime_feed_pcm(&m->q,pcm,2));
    snprintf(wire,sizeof(wire),"{\"type\":\"conversation.item.input_audio_transcription.delta\",\"item_id\":\"i%u\",\"text\":\"hi\"}",turn);
    assert(!fragment(m,wire,1,7));
    assert(!agent_realtime_commit(&m->q));
    snprintf(wire,sizeof(wire),"{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"i%u\"}",turn);
    assert(!whole(m,wire));
    snprintf(wire,sizeof(wire),"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"i%u\",\"transcript\":\"hi\"}",turn);
    assert(!fragment(m,wire,3,7));
    snprintf(wire,sizeof(wire),"{\"type\":\"response.created\",\"response\":{\"id\":\"r%u\"}}",turn);
    assert(!whole(m,wire));
    snprintf(wire,sizeof(wire),"{\"type\":\"response.audio.delta\",\"response_id\":\"r%u\",\"delta\":\"EjR4Vg==\"}",turn);
    assert(!whole(m,wire));
    snprintf(wire,sizeof(wire),"{\"type\":\"response.done\",\"response\":{\"id\":\"r%u\",\"status\":\"completed\"}}",turn);
    assert(!whole(m,wire));
}
static void turn_reuse(void)
{
    static const char *clear="{\"type\":\"input_audio_buffer.cleared\"}";
    mock_t *m=setup(true);m->q.reusable=true;
    for(unsigned turn=1;turn<=3;++turn) {
        reusable_turn(m,turn);
        assert(m->q.input_count==turn && m->q.response_count==turn && m->samples==2*turn);
        if(turn==3) {
            assert(agent_realtime_next_turn(&m->q)==AGENT_ERR_PROTOCOL);break;
        }
        m->incoming_at=0;m->incoming_count=1;m->incoming[0]=clear;
        unsigned sends=m->sends,nonce=m->speech.nonce;
        assert(!agent_realtime_next_turn(&m->q));
        assert(m->sends==sends+1 && m->speech.nonce==nonce+1 && !m->closes);
        assert(!m->q.input_started && !m->q.input_final && !m->q.done && !m->q.message);
        assert(!m->q.input_item_id[0] && !m->q.input_samples && m->q.ready);
    }
    agent_realtime_cancel(&m->q);assert(m->closes==1);free(m);
    for(unsigned fault=0;fault<8;++fault) {
        m=setup(true);m->q.reusable=true;reusable_turn(m,1);
        if(fault==0)m->q.input_final=false;
        if(fault==1)m->q.active=true;
        if(fault==2)m->q.message=true;
        if(fault==3)m->q.odd=true;
        if(fault<4)assert(agent_realtime_next_turn(&m->q)==AGENT_ERR_PROTOCOL && !m->closes);
        else {
            if(fault==4) {
                uint64_t at=m->ticks;
                assert(agent_realtime_next_turn(&m->q)==AGENT_ERR_TIMEOUT && m->ticks-at==1000);
            } else {
                m->incoming_count=1;m->incoming[0]=fault==5?
                    "{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"i1\",\"transcript\":\"late\"}":clear;
                if(fault==5)assert(agent_realtime_next_turn(&m->q)==AGENT_ERR_PROTOCOL);
                else {
                    assert(!agent_realtime_next_turn(&m->q));
                    int16_t pcm[2]={1,2};assert(!agent_realtime_feed_pcm(&m->q,pcm,2));
                    if(fault==6)assert(whole(m,"{\"type\":\"conversation.item.input_audio_transcription.delta\",\"item_id\":\"i1\",\"text\":\"old\"}")==AGENT_ERR_PROTOCOL);
                    else {
                        assert(!agent_realtime_commit(&m->q));
                        assert(whole(m,"{\"type\":\"response.created\",\"response\":{\"id\":\"r1\"}}")==AGENT_ERR_PROTOCOL);
                    }
                }
            }
            assert(m->closes==1 && m->samples==2);
        }
        free(m);
    }
}
static void append_record_budget(void)
{
    /* Feed real encoder output into the real frame packer and audio decoder.
     * This proves payload size/sample integrity, not TLS/network behaviour. */
    const unsigned blocks[]={512,464};
    const uint32_t nonces[]={0,9999,UINT32_MAX-1};
    for(unsigned size=0;size<2;++size)for(unsigned seq=0;seq<3;++seq) {
        mock_t *m=setup(true);unsigned count=blocks[size];m->speech.nonce=nonces[seq];
        int16_t pcm[512];for(unsigned i=0;i<count;++i)pcm[i]=(int16_t)(i*431-32700);
        assert(!agent_realtime_feed_pcm(&m->q,pcm,count));
        assert(m->sends==1 && m->q.input_samples==count);
        size_t json_bytes=strlen(m->sent[0]);uint8_t frame[AGENT_WS_FRAME_MAX];
        const uint8_t mask[4]={0x12,0x34,0x56,0x78};
        size_t framed=agent_ws_frame_pack(frame,sizeof(frame),false,m->sent[0],json_bytes,mask);
        assert(framed==json_bytes+8);
        if(count==464)assert(framed+85+12<=1440);
        else assert(framed+21>1440); /* Even a small AEAD record exceeds MSS. */
        started(m);
        cJSON *packet=agent_json_parse(m->sent[0],json_bytes);assert(packet);
        const char *audio=agent_json_string(packet,"audio");assert(audio);
        char reply[1900];snprintf(reply,sizeof(reply),
            "{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"%s\"}",audio);
        assert(!whole(m,reply) && m->samples==count && !memcmp(pcm,m->pcm,count*sizeof(*pcm)));
        printf("append_record samples=%u nonce=%lu json=%zu websocket=%zu tls_max_with_ts=%zu\n",
            count,(unsigned long)nonces[seq],json_bytes,framed,framed+85+12);
        cJSON_Delete(packet);free(m);
    }
    /* Several full optimized blocks followed by a non-aligned73-sample tail. */
    mock_t *m=setup(true);int16_t source[2*464+73];
    for(unsigned i=0;i<sizeof(source)/sizeof(*source);++i)source[i]=(int16_t)(i*997+13);
    for(unsigned at=0;at<sizeof(source)/sizeof(*source);) {
        unsigned n=sizeof(source)/sizeof(*source)-at;if(n>464)n=464;
        assert(!agent_realtime_feed_pcm(&m->q,source+at,n));at+=n;
    }
    assert(m->sends==3 && m->q.input_samples==sizeof(source)/sizeof(*source));
    started(m);
    for(unsigned i=0;i<m->sends;++i) {
        cJSON *packet=agent_json_parse(m->sent[i],strlen(m->sent[i]));assert(packet);
        char reply[1900];snprintf(reply,sizeof(reply),
            "{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"%s\"}",
            agent_json_string(packet,"audio"));
        assert(!whole(m,reply));cJSON_Delete(packet);
    }
    assert(m->samples==sizeof(source)/sizeof(*source) && !memcmp(source,m->pcm,sizeof(source)));
    free(m);
}
static void manual_control(void)
{
    static const char *preview="{\"type\":\"conversation.item.input_audio_transcription.delta\",\"item_id\":\"pending_audio\",\"text\":\"\",\"stash\":\"请把灯调成蓝色\"}";
    static const char *updated="{\"type\":\"session.updated\",\"session\":{\"turn_detection\":null," FORMAT16 "}}";
    for(unsigned active=0;active<2;++active) {
        mock_t *m=setup(true);int16_t pcm[2]={37,-61};m->q.manual_draft=true;
        assert(!agent_realtime_feed_pcm(&m->q,pcm,2));
        assert(!agent_realtime_create_response(&m->q));
        if(active)assert(!whole(m,response_created));
        unsigned sends=m->sends;
        assert(agent_realtime_commit(&m->q)==AGENT_ERR_BUSY);
        assert(m->sends==sends && m->q.input_samples==2);
        assert(!agent_realtime_commit_input(&m->q));
        assert(m->sends==sends+1 && !m->q.input_samples && m->q.active==!!active);
        assert(agent_realtime_commit_input(&m->q)==AGENT_ERR_PROTOCOL);
        assert(!whole(m,"{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"whole_input\"}"));
        assert(!whole(m,"{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"whole_input\",\"transcript\":\"final input\"}"));
        if(!active)assert(!whole(m,response_created));
        assert(!whole(m,finished));
        assert(m->q.done && m->sends==sends+1 && !m->closes && !m->samples);
        agent_realtime_cancel(&m->q);free(m);
    }
    for(unsigned split=1;split<=113;split=split==1?7:113) {
        mock_t *m=setup(true);int16_t pcm[2]={37,-61};
        assert(agent_realtime_commit_input(&m->q)==AGENT_ERR_PROTOCOL && !m->sends);
        assert(!agent_realtime_feed_pcm(&m->q,pcm,2));
        size_t cut=strlen(preview)/2;
        agent_ws_chunk_t part={cut,1,true,false};
        assert(!agent_realtime_receive(&m->q,preview,&part));
        assert(m->q.message && m->q.used==cut);
        uint64_t deadline=m->q.deadline;
        assert(!agent_realtime_update(&m->q,"草稿：\"蓝色\"\n","[]"));
        assert(m->q.updating && m->q.ready && m->q.input_samples==2 && m->q.used==cut);
        assert(m->q.deadline==deadline && !m->q.pending && !m->samples);
        unsigned sends=m->sends;
        assert(agent_realtime_update(&m->q,"second","[]")==AGENT_ERR_BUSY);
        assert(agent_realtime_create_response(&m->q)==AGENT_ERR_BUSY);
        assert(agent_realtime_commit_input(&m->q)==AGENT_ERR_BUSY);
        assert(agent_realtime_tool_result(&m->q,"tool","{}")==AGENT_ERR_BUSY);
        assert(m->sends==sends);
        part=(agent_ws_chunk_t){strlen(preview)-cut,0,false,true};
        assert(!agent_realtime_receive(&m->q,preview+cut,&part));
        assert(!agent_realtime_feed_pcm(&m->q,pcm,2) && m->q.input_samples==4);
        assert(!fragment(m,updated,split,17) && !m->q.updating);
        cJSON *root=agent_json_parse(m->sent[1],strlen(m->sent[1]));assert(root);
        const cJSON *config=cJSON_GetObjectItemCaseSensitive(root,"session");
        assert(!strcmp(agent_json_string(config,"instructions"),"草稿：\"蓝色\"\n"));
        assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(config,"turn_detection")));
        cJSON_Delete(root);
        /* Request the draft without committing or losing the four samples. */
        assert(!agent_realtime_create_response(&m->q) && m->q.input_samples==4);
        assert(!fragment(m,response_created,split,19));
        assert(agent_realtime_update(&m->q,"x","[]")==AGENT_ERR_BUSY);
        assert(agent_realtime_commit_input(&m->q)==AGENT_ERR_BUSY);
        assert(!agent_realtime_feed_pcm(&m->q,pcm,2) && m->q.input_samples==6);
        assert(!fragment(m,finished,split,23));
        sends=m->sends;
        assert(!agent_realtime_commit_input(&m->q));
        assert(m->sends==sends+1 && !m->q.pending && !m->q.active && !m->q.input_samples);
        assert(strstr(m->sent[sends],"input_audio_buffer.commit"));
        assert(agent_realtime_commit_input(&m->q)==AGENT_ERR_PROTOCOL && m->sends==sends+1);
        assert(!agent_realtime_create_response(&m->q) && m->sends==sends+2);
        assert(strstr(m->sent[sends+1],"response.create"));
        assert(!m->samples && !m->tool_events && !m->closes);
        agent_realtime_cancel(&m->q);free(m);
        if(split==113)break;
    }
    for(unsigned fault=0;fault<10;++fault) {
        mock_t *m=setup(true);
        if(fault==0 || fault==1) {
            m->q.auto_vad=fault==0;m->q.continuous=fault==1;
            assert(agent_realtime_update(&m->q,"x","[]")==AGENT_ERR_ARGUMENT);
        } else if(fault==2) {
            assert(agent_realtime_update(&m->q,"x","{}")==AGENT_ERR_JSON);
        } else if(fault==3) {
            char text[2048];memset(text,'x',sizeof(text)-1);text[sizeof(text)-1]=0;
            assert(agent_realtime_update(&m->q,text,"[]")==AGENT_ERR_LIMIT && m->closes==1);
        } else if(fault==4) {
            m->send_error=AGENT_ERR_NETWORK;
            assert(agent_realtime_update(&m->q,"x","[]")==AGENT_ERR_NETWORK && m->attempts==1 && m->closes==1);
        } else if(fault==5) {
            atomic_store(&m->cancelled,true);
            assert(agent_realtime_update(&m->q,"x","[]")==AGENT_ERR_CANCELLED);
        } else if(fault==6) {
            assert(!agent_realtime_update(&m->q,"x","[]"));
            m->ticks=m->q.deadline;
            assert(agent_realtime_poll(&m->q,1)==AGENT_ERR_TIMEOUT && m->closes==1);
        } else if(fault==7) {
            assert(whole(m,updated)==AGENT_ERR_PROTOCOL && m->closes==1);
        } else if(fault==8) {
            assert(!agent_realtime_update(&m->q,"x","[]"));
            m->callback_error=AGENT_ERR_PROTOCOL; /* Caller rejects bad config echo. */
            assert(whole(m,updated)==AGENT_ERR_PROTOCOL && m->closes==1);
        } else {
            m->busy=7;m->incoming[0]=updated;m->incoming_count=1;
            assert(agent_realtime_update(&m->q,"x","[]")==AGENT_ERR_PROTOCOL);
            assert(!m->sends && !m->q.updating && m->closes==1);
        }
        assert(!m->samples && !m->tool_events);free(m);
    }
}
static void manual_reuse(void)
{
    static const char *a="{\"type\":\"conversation.item.input_audio_transcription.delta\",\"item_id\":\"preview_a\",\"text\":\"介绍\",\"stash\":\"\"}";
    static const char *b="{\"type\":\"conversation.item.input_audio_transcription.delta\",\"item_id\":\"preview_b\",\"text\":\"介绍自己\",\"stash\":\"\"}";
    static const char *ack="{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"final_i\"}";
    static const char *final="{\"type\":\"conversation.item.input_audio_transcription.completed\",\"item_id\":\"final_i\",\"transcript\":\"介绍你自己\"}";
    static const char *clear="{\"type\":\"input_audio_buffer.cleared\"}";
    for(unsigned fault=0;fault<7;++fault) {
        mock_t *m=setup(true);m->q.manual_draft=m->q.reusable=true;int16_t input[2]={1,2};
        if(fault==1)assert(whole(m,a)==AGENT_ERR_PROTOCOL); /* Idle preview. */
        else {
            assert(!agent_realtime_feed_pcm(&m->q,input,2));
            assert(!whole(m,a) && !whole(m,b));
            assert(!m->q.input_count && !m->q.input_item_id[0]);
            assert(!agent_realtime_create_response(&m->q));
            assert(!whole(m,response_created));
            if(fault==2)assert(whole(m,final)==AGENT_ERR_PROTOCOL);
            else if(fault==3)assert(whole(m,ack)==AGENT_ERR_PROTOCOL);
            else {
                assert(!agent_realtime_commit_input(&m->q));
                assert(!whole(m,ack) && m->q.input_count==1);
                if(fault==4)assert(whole(m,b)==AGENT_ERR_PROTOCOL); /* Commit identity now binds. */
                else {
                    assert(!whole(m,final) && m->q.input_final && !whole(m,finished));
                    if(fault==5)assert(whole(m,final)==AGENT_ERR_PROTOCOL);
                    else {
                        m->incoming[0]=clear;m->incoming_count=1;
                        assert(!agent_realtime_next_turn(&m->q));
                        assert(m->q.manual_draft && m->q.reusable && m->q.input_count==1);
                        assert(!agent_realtime_feed_pcm(&m->q,input,2));
                        if(fault==6)assert(whole(m,final)==AGENT_ERR_PROTOCOL);
                        else assert(!whole(m,a) && !m->q.input_item_id[0]);
                    }
                }
            }
        }
        assert(!m->samples && !m->tool_events);
        agent_realtime_cancel(&m->q);assert(m->closes==1);free(m);
    }
}
static void draft_reuse_wire(const char *path,unsigned missing_final_responses)
{
    FILE *file=fopen(path,"rb");assert(file);
    char *line=malloc(262144);assert(line);
    mock_t *m=setup(true);m->q.created=m->q.ready=false;
    m->q.reusable=m->q.manual_draft=true;
    unsigned events=0;bool clearing=false;int16_t input[512]={0};
    while(fgets(line,262144,file)) {
        assert(strchr(line,'\n'));
        cJSON *record=agent_json_parse(line,strlen(line));assert(record);
        const char *direction=agent_json_string(record,"direction");assert(direction);
        const cJSON *value=!strcmp(direction,"send")?record:cJSON_GetObjectItemCaseSensitive(record,"event");
        const char *type=agent_json_string(value,"type");assert(type);
        if(!strcmp(direction,"send")) {
            m->sends=0; /* Preserve nonce/state, discard only mock transmission copies. */
            if(!strcmp(type,"input_audio_buffer.append")) {
                uint64_t samples=0;assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(value,"samples"),512,&samples) && samples);
                assert(!agent_realtime_feed_pcm(&m->q,input,(size_t)samples));
            } else if(!strcmp(type,"response.create"))assert(!agent_realtime_create_response(&m->q));
            else if(!strcmp(type,"input_audio_buffer.commit"))assert(!agent_realtime_commit_input(&m->q));
            else if(!strcmp(type,"input_audio_buffer.clear")) {
                assert(!clearing);
                if(missing_final_responses) {
                    /* The host observer advances after a failed turn only to
                     * collect evidence. Product reuse must reject that boundary. */
                    assert(m->q.done && m->q.input_ended && !m->q.input_final);
                    assert(m->q.input_count==1 && m->q.response_count==missing_final_responses &&
                           m->done_events==missing_final_responses);
                    assert(m->samples && !m->tool_events && !m->closes);
                    assert(agent_realtime_next_turn(&m->q)==AGENT_ERR_PROTOCOL && !m->sends);
                    printf("Actual missing-final wire: %u events,%zu decoded PCM samples; reuse rejected before clear; host only\n",events,m->samples);
                    agent_realtime_cancel(&m->q);assert(m->closes==1);
                    cJSON_Delete(record);fclose(file);free(line);free(m);return;
                }
                clearing=true;
            }
            else assert(!strcmp(type,"session.update"));
        } else {
            char *event_text=cJSON_PrintUnformatted(value);assert(event_text);
            if(clearing) {
                assert(!strcmp(type,"input_audio_buffer.cleared"));
                m->incoming_at=0;m->incoming_count=1;m->incoming[0]=event_text;
                assert(!agent_realtime_next_turn(&m->q));m->incoming_count=0;clearing=false;
            } else {
                agent_err_t e=fragment(m,event_text,127,events*7+1);
                if(e)fprintf(stderr,"draft reuse wire event%u %s: %s\n",events,type,agent_err_name(e));
                assert(!e);
            }
            free(event_text);++events;
        }
        cJSON_Delete(record);
    }
    assert(!missing_final_responses);
    assert(!ferror(file) && !clearing && m->q.input_count==3 && m->q.response_count==3 && m->done_events==3);
    assert(m->q.done && m->q.input_final && m->samples && !m->tool_events && !m->closes);
    assert(agent_realtime_next_turn(&m->q)==AGENT_ERR_PROTOCOL);
    printf("Actual draft reuse wire: %u events,3 complete inputs/responses,%zu PCM samples; host only\n",events,m->samples);
    agent_realtime_cancel(&m->q);assert(m->closes==1);fclose(file);free(line);free(m);
}
static void reuse_wire(const char *path)
{
    FILE *file=fopen(path,"rb");assert(file);
    char *line=malloc(262144);assert(line);
    mock_t *m=setup(true);m->q.created=m->q.ready=false;m->q.reusable=true;
    unsigned turns=0,events=0;int16_t input[2]={1,2};
    while(fgets(line,262144,file)) {
        assert(strchr(line,'\n'));
        cJSON *record=agent_json_parse(line,strlen(line));assert(record);
        const char *type=agent_json_string(record,"type");assert(type);
        if(!strcmp(type,"input_audio_buffer.cleared")) {
            assert(m->q.input_final && m->q.done);
            if(turns==3) {assert(agent_realtime_next_turn(&m->q)==AGENT_ERR_PROTOCOL);cJSON_Delete(record);break;}
            m->incoming_at=0;m->incoming_count=1;m->incoming[0]=line;
            assert(!agent_realtime_next_turn(&m->q));
            assert(!agent_realtime_feed_pcm(&m->q,input,2));
            assert(!agent_realtime_commit(&m->q));
        } else {
            agent_err_t e=fragment(m,line,127,events*7+1);
            if(e)fprintf(stderr,"reuse wire event%u %s: %s\n",events,type,agent_err_name(e));
            assert(!e);
            if(!strcmp(type,"session.updated")) {
                assert(!agent_realtime_feed_pcm(&m->q,input,2));
                assert(!agent_realtime_commit(&m->q));
            }
            if(!strcmp(type,"response.done"))++turns;
        }
        cJSON_Delete(record);++events;
    }
    assert(turns==3 && m->q.input_count==3 && m->q.response_count==3 && m->done_events==3);
    assert(m->samples && !m->tool_events && !m->closes);
    printf("Actual same-session wire: %u events,3 complete inputs/responses,%zu PCM samples; host only\n",events,m->samples);
    agent_realtime_cancel(&m->q);assert(m->closes==1);
    fclose(file);free(line);free(m);
}
static void output_format(void)
{
    static const char *const cases[]={
        "{}", "{\"audio\":null}", "{\"audio\":{\"output\":{}}}",
        "{\"audio\":{\"output\":{\"format\":{\"type\":\"pcm\",\"sample_rate\":24000}}}}",
        "{\"audio\":{\"output\":{\"format\":{\"type\":\"opus\",\"sample_rate\":16000}}}}",
        "{\"audio\":{\"output\":{\"format\":{\"type\":\"pcm\",\"sample_rate\":16000.5}}}}",
        "{\"audio\":{\"output\":{\"format\":{\"type\":\"pcm\",\"sample_rate\":\"16000\"}}}}"
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        for(unsigned updating=0;updating<2;++updating) {
            mock_t *m=setup(true);m->q.ready=updating;m->q.updating=updating;
            char wire[256];snprintf(wire,sizeof(wire),"{\"type\":\"session.updated\",\"session\":%s}",cases[i]);
            assert(fragment(m,wire,7,11)==AGENT_ERR_PROTOCOL);
            assert(!m->samples && !m->pcm_opens && m->closes==1);free(m);
        }
    }
}
static const char *text_ack="{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"server_text1\",\"status\":\"completed\",\"type\":\"message\",\"role\":\"user\",\"content\":[{\"type\":\"input_text\",\"text\":\"你好，小言。\"}]}}";
static void text_configuration_test(void)
{
    const char config[]="{\"type\":\"session.updated\",\"session\":{"
        "\"model\":\"" AGENT_RT_MODEL "\",\"modalities\":[\"text\",\"audio\"],"
        "\"voice\":\"Tina\",\"turn_detection\":null,\"enable_search\":false,"
        "\"max_tokens\":96,\"instructions\":\"short\"," FORMAT16 "}}";
    const struct {const char *key,*value;} bad[]={
        {"model","\"wrong\""},{"voice","\"Cherry\""},{"max_tokens","97"},
        {"max_tokens","\"96\""},{"max_tokens","null"},{"enable_search","true"},
        {"turn_detection","{}"},{"tools","[{}]"},{"tools","null"},
        {"instructions","\"changed\""},{"modalities","[\"audio\"]"},
        {"modalities","[\"text\",\"audio\",\"text\"]"}
    };
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad)+2;++i) {
        mock_t *m=setup(false);cJSON *root=agent_json_parse(config,strlen(config));assert(root);
        cJSON *session=cJSON_GetObjectItemCaseSensitive(root,"session");
        if(i>=2) {
            cJSON_DeleteItemFromObjectCaseSensitive(session,bad[i-2].key);
            assert(cJSON_AddItemToObject(session,bad[i-2].key,agent_json_parse(bad[i-2].value,strlen(bad[i-2].value))));
        } else if(i==1)assert(cJSON_AddItemToObject(session,"tools",cJSON_CreateArray()));
        char *wire=cJSON_PrintUnformatted(root);assert(wire);m->configuration=wire;
        agent_err_t error=agent_realtime_begin_text(&m->q,"short");
        assert(error==(i>=2?AGENT_ERR_PROTOCOL:AGENT_OK));
        assert(m->sends==1 && !m->samples && !m->pcm_opens);
        cJSON *sent=agent_json_parse(m->sent[0],strlen(m->sent[0]));
        const cJSON *sent_session=cJSON_GetObjectItemCaseSensitive(sent,"session");
        uint64_t tokens=0;assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(sent_session,"max_tokens"),96,&tokens) && tokens==96);
        cJSON_Delete(sent);
        if(!error) {
            int16_t pcm=0;assert(agent_realtime_feed_pcm(&m->q,&pcm,1)==AGENT_ERR_PROTOCOL);
            assert(agent_realtime_update(&m->q,"changed","[]")==AGENT_ERR_PROTOCOL);
            assert(agent_realtime_begin_text(&m->q,"short")==AGENT_ERR_BUSY);
            agent_realtime_cancel(&m->q);
        }
        assert(m->closes==1);free(wire);cJSON_Delete(root);free(m);
    }
}
static void text_candidate(void)
{
    mock_t *m=setup(true);const char *text="你好，小言。";
    assert(agent_realtime_user_text(&m->q,NULL)==AGENT_ERR_ARGUMENT);
    assert(agent_realtime_user_text(&m->q,"")==AGENT_ERR_ARGUMENT);
    assert(agent_realtime_user_text(&m->q,"\xff")==AGENT_ERR_ARGUMENT);
    char large[514];memset(large,'a',sizeof(large));large[513]=0;
    assert(agent_realtime_user_text(&m->q,large)==AGENT_ERR_ARGUMENT);
    m->busy=2;assert(!agent_realtime_user_text(&m->q,text));
    assert(m->sends==1 && m->attempts==3 && m->q.text_input==text && !m->q.text_ready);
    cJSON *wire=agent_json_parse(m->sent[0],strlen(m->sent[0]));assert(wire);
    const cJSON *item=cJSON_GetObjectItemCaseSensitive(wire,"item");
    assert(!strcmp(agent_json_string(wire,"type"),"conversation.item.create"));
    assert(!agent_json_string(item,"id"));
    assert(!strcmp(agent_json_string(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(item,"content"),0),"text"),text));
    cJSON_Delete(wire);
    assert(agent_realtime_create_response(&m->q)==AGENT_ERR_BUSY && m->sends==1);
    assert(agent_realtime_user_text(&m->q,text)==AGENT_ERR_PROTOCOL);
    assert(!fragment(m,text_ack,1,7) && m->q.text_ready && !m->q.input_final);
    assert(!strcmp(m->q.input_item_id,"server_text1"));
    assert(!agent_realtime_create_response(&m->q));started(m);
    int16_t sample=1;assert(agent_realtime_feed_pcm(&m->q,&sample,1)==AGENT_ERR_PROTOCOL);
    assert(agent_realtime_tool_result(&m->q,"call","{}")==AGENT_ERR_PROTOCOL);
    assert(!whole(m,"{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"EjR4Vg==\"}"));
    assert(!whole(m,finished));assert(m->samples==2 && m->done_events==1 && !m->tool_events);
    assert(agent_realtime_create_response(&m->q)==AGENT_ERR_PROTOCOL);
    assert(agent_realtime_update(&m->q,"replacement","[]")==AGENT_ERR_PROTOCOL);
    agent_realtime_cancel(&m->q);assert(m->closes==1);free(m);

    const char *bad[]={
        "{\"id\":\"\",\"status\":\"completed\",\"role\":\"user\",\"type\":\"message\",\"content\":[{\"type\":\"input_text\",\"text\":\"你好，小言。\"}]}",
        "{\"id\":\"text1\",\"status\":\"completed\",\"role\":\"user\",\"type\":\"message\",\"content\":[{\"type\":\"input_text\",\"text\":\"别的输入\"}]}",
        "{\"id\":\"text1\",\"status\":\"completed\",\"role\":\"user\",\"type\":\"message\",\"content\":[]}",
        "{\"id\":\"text1\",\"status\":\"in_progress\",\"role\":\"user\",\"type\":\"message\",\"content\":[{\"type\":\"input_text\",\"text\":\"你好，小言。\"}]}",
        "{\"id\":\"text1\",\"role\":\"assistant\",\"type\":\"message\",\"content\":[]}",
        "{\"id\":\"text1\",\"type\":\"message\"}"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        m=setup(true);assert(!agent_realtime_user_text(&m->q,text));
        char event_text[512];snprintf(event_text,sizeof(event_text),"{\"type\":\"conversation.item.created\",\"item\":%s}",bad[i]);
        assert(fragment(m,event_text,7,i)==AGENT_ERR_PROTOCOL && m->closes==1 && !m->samples);free(m);
    }
    const char *invalid[]={text_ack,"{\"type\":\"input_audio_buffer.committed\"}",
        "{\"type\":\"conversation.item.input_audio_transcription.completed\"}"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        m=setup(true);assert(!agent_realtime_user_text(&m->q,text));assert(!whole(m,text_ack));
        assert(whole(m,invalid[i])==AGENT_ERR_PROTOCOL && m->closes==1);free(m);
    }
    m=setup(true);m->q.input_samples=1;
    assert(agent_realtime_user_text(&m->q,text)==AGENT_ERR_PROTOCOL && !m->sends);free(m);
    m=setup(true);m->send_error=AGENT_ERR_NETWORK;
    assert(agent_realtime_user_text(&m->q,text)==AGENT_ERR_NETWORK);
    assert(!m->q.text_input && m->closes==1 && m->attempts==1);free(m);
    m=setup(true);atomic_store(&m->cancelled,true);
    assert(agent_realtime_user_text(&m->q,text)==AGENT_ERR_CANCELLED && !m->sends);free(m);
    m=setup(true);char escaped[513];memset(escaped,1,512);escaped[512]=0;
    assert(agent_realtime_user_text(&m->q,escaped)==AGENT_ERR_LIMIT && !m->sends && m->closes==1);free(m);
}
#if AGENT_RT_TEXT_REUSE
static mock_t *reusable_text(void)
{
    mock_t *m=setup(true);m->q.text_reuse=true;m->q.text_instructions="short";return m;
}
static void text_pipeline(void)
{
    const char *text="你好，小言。";
    /* Receiving an unacknowledged response, PCM or wrong item must close the
     * session before any sink write. Sending early never grants authority. */
    const char *bad[]={
        "{\"type\":\"response.created\",\"response\":{\"id\":\"r1\"}}",
        "{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"EjR4Vg==\"}",
        "{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"u1\",\"status\":\"completed\",\"role\":\"user\",\"type\":\"message\",\"content\":[{\"type\":\"input_text\",\"text\":\"不同的输入\"}]}}",
        "{\"type\":\"response.audio_transcript.delta\",\"response_id\":\"r1\",\"delta\":\"旧回答\"}"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        mock_t *m=reusable_text();assert(!agent_realtime_user_text(&m->q,text));
        assert(!agent_realtime_create_response(&m->q) && m->q.pending && !m->q.text_ready);
        assert(m->sends==2 && !m->reads);
        assert(fragment(m,bad[i],1,i)==AGENT_ERR_PROTOCOL && m->closes==1 && !m->samples);
        assert(agent_realtime_create_response(&m->q)==AGENT_ERR_PROTOCOL && m->sends==2);free(m);
    }
    /* Text accepted locally, then second write fails/cancels/expires: never
     * replay the text or expose speculative output. Absolute cap is retained. */
    for(unsigned i=0;i<3;++i) {
        mock_t *m=reusable_text();assert(!agent_realtime_user_text(&m->q,text));
        agent_err_t expected;
        if(!i) {m->send_error=AGENT_ERR_NETWORK;expected=AGENT_ERR_NETWORK;}
        else if(i==1) {atomic_store(&m->cancelled,true);expected=AGENT_ERR_CANCELLED;}
        else {m->q.deadline_cap=100;m->block_after_sends=1;expected=AGENT_ERR_TIMEOUT;}
        assert(agent_realtime_create_response(&m->q)==expected);
        assert(m->sends==1 && m->closes==1 && !m->samples && !m->q.text_ready);
        assert(agent_realtime_user_text(&m->q,text)==expected && m->sends==1);free(m);
    }
}
static void reuse_text_ack(mock_t *m,unsigned id)
{
    char wire[256];snprintf(wire,sizeof(wire),
        "{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"u%u\",\"status\":\"completed\","
        "\"type\":\"message\",\"role\":\"user\",\"content\":[{\"type\":\"input_text\",\"text\":\"你好，小言。\"}]}}",id);
    assert(!fragment(m,wire,13,id));
}
static void reuse_text_response(mock_t *m,unsigned id)
{
    char wire[192];
    snprintf(wire,sizeof(wire),"{\"type\":\"response.created\",\"response\":{\"id\":\"r%u\"}}",id);
    assert(!fragment(m,wire,7,id));
    snprintf(wire,sizeof(wire),"{\"type\":\"response.audio.delta\",\"response_id\":\"r%u\",\"delta\":\"EjR4Vg==\"}",id);
    assert(!fragment(m,wire,1,id));
    snprintf(wire,sizeof(wire),"{\"type\":\"response.done\",\"response\":{\"id\":\"r%u\",\"status\":\"completed\"}}",id);
    assert(!fragment(m,wire,11,id));
}
static void text_reuse(void)
{
    const char *text="你好，小言。";
    mock_t *m=reusable_text();assert(agent_realtime_next_text_turn(&m->q)==AGENT_ERR_PROTOCOL);
    assert(agent_realtime_create_response(&m->q)==AGENT_ERR_PROTOCOL);
    assert(agent_realtime_tool_result(&m->q,"stale_tool","{}")==AGENT_ERR_PROTOCOL);
    m->q.deadline_cap=100000;
    for(unsigned i=1;i<=3;++i) {
        assert(!agent_realtime_user_text(&m->q,text));
        assert(agent_realtime_next_text_turn(&m->q)==AGENT_ERR_PROTOCOL);
        assert(!agent_realtime_create_response(&m->q));
        assert(m->q.pending && !m->q.text_ready && !m->reads && m->sends==2*i);
        assert(agent_realtime_create_response(&m->q)==AGENT_ERR_BUSY && m->sends==2*i);
        reuse_text_ack(m,i);reuse_text_response(m,i);
        assert(m->q.input_count==i && m->q.response_count==i && m->q.done && m->samples==i*2);
        agent_realtime_t saved=m->q;
        bool *incomplete[]={&m->q.message,&m->q.odd,&m->q.active,&m->q.pending,&m->q.updating,&m->q.clearing};
        for(unsigned j=0;j<sizeof(incomplete)/sizeof(*incomplete);++j) {
            *incomplete[j]=true;
            assert(agent_realtime_next_text_turn(&m->q)==AGENT_ERR_PROTOCOL);m->q=saved;
        }
        unsigned sends=m->sends;agent_err_t error=agent_realtime_next_text_turn(&m->q);
        assert(error==(i==3?AGENT_ERR_LIMIT:AGENT_OK));
        assert(m->q.deadline_cap==100000 && m->q.input_count==i && m->q.response_count==i);
        assert(m->sends==sends && !m->closes && !m->reads); /* Reset performs no I/O. */
        if(i<3) {
            assert(!m->q.text_ready && !m->q.text_input && !m->q.done);
            assert(agent_realtime_next_text_turn(&m->q)==AGENT_ERR_PROTOCOL);
            assert(agent_realtime_create_response(&m->q)==AGENT_ERR_PROTOCOL);
            assert(agent_realtime_tool_result(&m->q,"stale_tool","{}")==AGENT_ERR_PROTOCOL);
        }
    }
    assert(agent_realtime_user_text(&m->q,text)==AGENT_ERR_PROTOCOL && m->sends==6);
    agent_realtime_cancel(&m->q);assert(m->closes==1);free(m);

    /* Even identical text cannot turn a receipt from the first turn into
     * authority for the second. Reject old item IDs, response IDs and PCM. */
    const char *stale[]={
        "{\"type\":\"conversation.item.created\",\"item\":{\"id\":\"u1\",\"status\":\"completed\",\"type\":\"message\",\"role\":\"user\",\"content\":[{\"type\":\"input_text\",\"text\":\"你好，小言。\"}]}}",
        "{\"type\":\"response.created\",\"response\":{\"id\":\"r1\"}}",
        "{\"type\":\"response.audio.delta\",\"response_id\":\"r1\",\"delta\":\"EjR4Vg==\"}",
        "{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"u1\"}"};
    for(unsigned kind=0;kind<5;++kind) {
        m=reusable_text();assert(!agent_realtime_user_text(&m->q,text));reuse_text_ack(m,1);
        assert(!agent_realtime_create_response(&m->q));reuse_text_response(m,1);
        assert(!agent_realtime_next_text_turn(&m->q));
        if(kind<2)assert(!agent_realtime_user_text(&m->q,text));
        if(kind==1) {reuse_text_ack(m,2);assert(!agent_realtime_create_response(&m->q));}
        assert(fragment(m,stale[kind==4?0:kind],3,kind)==AGENT_ERR_PROTOCOL);
        assert(m->closes==1 && m->samples==2);free(m);
    }
    for(unsigned expire=0;expire<2;++expire) {
        m=reusable_text();assert(!agent_realtime_user_text(&m->q,text));reuse_text_ack(m,1);
        assert(!agent_realtime_create_response(&m->q));reuse_text_response(m,1);
        if(expire) {m->ticks=100;m->q.deadline_cap=100;}
        else atomic_store(&m->cancelled,true);
        assert(agent_realtime_next_text_turn(&m->q)==(expire?AGENT_ERR_TIMEOUT:AGENT_ERR_CANCELLED));
        assert(m->closes==1 && m->sends==2);free(m);
    }
    m=setup(false);m->q.text_reuse=true;
    assert(agent_realtime_begin(&m->q,"x","[]",false)==AGENT_ERR_ARGUMENT && !m->opens);free(m);
}
static void text_reuse_wire(const char *directory)
{
    char path[1024];char *line=malloc(262144);assert(line);
    int n=snprintf(path,sizeof(path),"%s/report.json",directory);assert(n>0 && (size_t)n<sizeof(path));
    FILE *file=fopen(path,"rb");assert(file);size_t count=fread(line,1,262143,file);assert(feof(file));fclose(file);
    cJSON *report=cJSON_ParseWithLength(line,count);assert(report);
    const cJSON *config=cJSON_GetObjectItemCaseSensitive(report,"config");
    bool pipeline=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(report,"pipelined_sender"));
    const char *instructions=agent_json_string(config,"instructions");assert(instructions);
    mock_t *m=reusable_text();m->q.created=m->q.ready=false;m->q.text_instructions=instructions;
    cJSON *request=NULL;unsigned events=0;
    for(unsigned turn=1;turn<=3;++turn) {
        cJSON_Delete(request);
        n=snprintf(path,sizeof(path),"%s/turn-%02u/request.json",directory,turn);assert(n>0 && (size_t)n<sizeof(path));
        file=fopen(path,"rb");assert(file);count=fread(line,1,4095,file);assert(feof(file));fclose(file);
        request=agent_json_parse(line,count);assert(request);
        const cJSON *item=cJSON_GetObjectItemCaseSensitive(request,"item");
        const cJSON *part=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(item,"content"),0);
        const char *text=agent_json_string(part,"text");assert(text && *text);
        if(turn>1) {
            assert(!agent_realtime_user_text(&m->q,text));
            if(pipeline)assert(!agent_realtime_create_response(&m->q));
        }
        n=snprintf(path,sizeof(path),"%s/turn-%02u/received.jsonl",directory,turn);assert(n>0 && (size_t)n<sizeof(path));
        file=fopen(path,"rb");assert(file);size_t before=m->samples;unsigned first_event=events;
        while(fgets(line,262144,file)) {
            assert(strchr(line,'\n'));cJSON *root=cJSON_ParseWithLength(line,strlen(line));assert(root);
            const char *type=agent_json_string(root,"type");assert(type);
            agent_err_t error=fragment(m,line,127,events*7+1);
            if(error)fprintf(stderr,"reuse wire turn%u/event%u %s: %s\n",turn,events,type,agent_err_name(error));
            assert(!error);
            if(!strcmp(type,"session.updated")) {
                assert(turn==1 && !agent_realtime_user_text(&m->q,text));
                if(pipeline)assert(!agent_realtime_create_response(&m->q));
            }
            if(!pipeline && m->q.text_ready && m->sends==turn*2-1)assert(!agent_realtime_create_response(&m->q));
            cJSON_Delete(root);++events;
        }
        assert(!ferror(file));fclose(file);
        assert(m->q.done && m->q.input_count==turn && m->q.response_count==turn && !m->closes && !m->tool_events);
        n=snprintf(path,sizeof(path),"%s/turn-%02u/output.pcm",directory,turn);assert(n>0 && (size_t)n<sizeof(path));
        file=fopen(path,"rb");assert(file);
        for(size_t i=before;i<m->samples;++i) {
            int lo=fgetc(file),hi=fgetc(file);assert(lo>=0 && hi>=0);
            assert(m->pcm[i]==(int16_t)((unsigned)lo|((unsigned)hi<<8)));
        }
        assert(fgetc(file)==EOF && !ferror(file));fclose(file);
        printf("Reused text turn%u: %u actual events,%zu exact PCM samples,serverID=%s\n",turn,events-first_event,m->samples-before,m->q.input_item_id);
        assert(agent_realtime_next_text_turn(&m->q)==(turn<3?AGENT_OK:AGENT_ERR_LIMIT));
    }
    assert(m->sends==6 && m->done_events==3 && !m->q.input_final && !m->q.input_samples);
    agent_realtime_cancel(&m->q);assert(m->closes==1);
    cJSON_Delete(request);cJSON_Delete(report);free(m);free(line);
}
#endif
static void text_wire(const char *directory)
{
    char path[1024];int n=snprintf(path,sizeof(path),"%s/request.json",directory);
    assert(n>0 && (size_t)n<sizeof(path));FILE *file=fopen(path,"rb");assert(file);
    char *line=malloc(262144);assert(line);size_t count=fread(line,1,4095,file);assert(feof(file));fclose(file);
    cJSON *request=agent_json_parse(line,count);assert(request);
    const cJSON *item=cJSON_GetObjectItemCaseSensitive(request,"item");
    const cJSON *content=cJSON_GetObjectItemCaseSensitive(item,"content");
    const char *text=agent_json_string(cJSON_GetArrayItem(content,0),"text");assert(text && *text);
    assert(!agent_json_string(item,"id"));
    mock_t *m=setup(true);m->q.created=m->q.ready=false;
    n=snprintf(path,sizeof(path),"%s/received.jsonl",directory);assert(n>0 && (size_t)n<sizeof(path));
    file=fopen(path,"rb");assert(file);unsigned events=0;
    while(fgets(line,262144,file)) {
        assert(strchr(line,'\n'));size_t length=strlen(line);
        /* Only the host envelope uses the large parser. Production receives
         * original wire bytes including original base64 order and fragments. */
        cJSON *event_json=cJSON_ParseWithLength(line,length);assert(event_json);
        const char *type=agent_json_string(event_json,"type");assert(type);
        agent_err_t error=fragment(m,line,127,events*7+1);
        if(error)fprintf(stderr,"text wire event%u %s: %s\n",events,type,agent_err_name(error));
        assert(!error);
        if(!strcmp(type,"session.updated"))assert(!agent_realtime_user_text(&m->q,text));
        if(m->q.text_ready && m->sends==1)assert(!agent_realtime_create_response(&m->q));
        cJSON_Delete(event_json);++events;
    }
    assert(!ferror(file));fclose(file);
    assert(m->q.text_ready && m->q.done && !m->q.input_final && !m->q.input_samples);
    assert(m->sends==2 && m->done_events==1 && !m->tool_events && m->samples);
    n=snprintf(path,sizeof(path),"%s/output.pcm",directory);assert(n>0 && (size_t)n<sizeof(path));
    file=fopen(path,"rb");assert(file);
    for(size_t i=0;i<m->samples;++i) {
        int lo=fgetc(file),hi=fgetc(file);assert(lo>=0 && hi>=0);
        assert(m->pcm[i]==(int16_t)((unsigned)lo|((unsigned)hi<<8)));
    }
    assert(fgetc(file)==EOF && !ferror(file));fclose(file);
    /* Exercise the proposed24KiB cache with the actual received PCM and
     * production encoder/decoder. It is lossy; do not call this PCM equality. */
    unsigned char *cache=malloc(24576);assert(cache);agent_speech_draft_t draft;
    agent_speech_draft_init(&draft,cache,24576);
    size_t offset=0;uint32_t seed=7;
    while(offset<m->samples) {
        seed=seed*1664525u+1013904223u;size_t count=1+seed%527;
        if(count>m->samples-offset)count=m->samples-offset;
        assert(!agent_speech_draft_write(&draft,m->pcm+offset,count));offset+=count;
    }
    assert(!draft.full && draft.samples==m->samples);
    int16_t decoded[128];size_t decoded_count=0,got;
    do {assert(!agent_speech_draft_read(&draft,decoded,128,&got));decoded_count+=got;} while(got);
    assert(decoded_count==m->samples);
    printf("Actual text wire: %u events,%zu exact PCM samples,serverID=%s; no playback authority\n",
        events,m->samples,m->q.input_item_id);
    printf("Actual24KiB candidate cache: %zu samples encoded/decoded,no overflow; lossy IMA\n",decoded_count);
    free(cache);
    agent_realtime_cancel(&m->q);assert(m->closes==1);
    cJSON_Delete(request);free(line);free(m);
}
int main(int argc,char **argv)
{
    streaming();metadata();errors();sending();auto_vad();tool_transition();transcription_failure();absolute_cap();turn_reuse();append_record_budget();manual_control();manual_reuse();output_format();text_candidate();text_configuration_test();
#if AGENT_RT_TEXT_REUSE
    text_reuse();text_pipeline();
    if(argc==3 && !strcmp(argv[1],"--text-reuse-wire")) {text_reuse_wire(argv[2]);return 0;}
#endif
    if(argc==3 && !strcmp(argv[1],"--text-wire"))text_wire(argv[2]);
    else if(argc==3 && !strcmp(argv[1],"--reuse-wire"))reuse_wire(argv[2]);
    else if(argc==3 && !strcmp(argv[1],"--draft-reuse-wire"))draft_reuse_wire(argv[2],0);
    else if(argc==3 && !strcmp(argv[1],"--missing-final-wire"))draft_reuse_wire(argv[2],1);
    else if(argc==3 && !strcmp(argv[1],"--confirmed-missing-final-wire"))draft_reuse_wire(argv[2],2);
    else if(argc>1)fixture(argv[1]);
    printf("Realtime C11: bounded streaming, UTF8, base64, tools, cancellation, no replay OK; session=%zu bytes\n",sizeof(agent_realtime_t));return 0;
}
