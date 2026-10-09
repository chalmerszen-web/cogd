#include "realtime.h"
#include "crc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char scratch[AGENT_RT_SCRATCH],trace[256];
static unsigned notices,closed;
static uint64_t clock_ms(void *ctx) { (void)ctx;return 1; }
static void close_ws(void *ctx) { (void)ctx;++closed; }
static void event(void *ctx,const char *stage,const char *text)
{
    (void)ctx;
    if(strcmp(stage,"realtime_state"))return;
    assert(text && strlen(text)<sizeof(trace) && !strstr(text,"private"));
    strcpy(trace,text);++notices;
}
static unsigned number(const cJSON *root,const char *key)
{
    uint64_t value;
    assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(root,key),UINT32_MAX,&value));
    return (unsigned)value;
}
static void check(unsigned mode)
{
    agent_speech_t speech={.scratch=scratch,.capacity=sizeof(scratch),.now_ms=clock_ms,.event=event};
    agent_ws_ops_t ws={.close=close_ws};agent_realtime_t q;
    agent_realtime_init(&q,&speech,&ws,NULL,NULL,NULL);
    q.opened=q.created=q.ready=q.active=q.continuous=true;q.deadline=1000;
    q.odd=mode==1;q.input_count=q.response_revision=2;q.pcm_bytes=mode==1?11:10;
    strcpy(q.response_id,"private-original");notices=closed=0;trace[0]=0;
    const char *wire=mode==2?
        "{\"type\":\"response.done\",\"response\":{\"id\":\"private-other\",\"status\":\"cancelled\"}}":
        mode==3?
        "{\"type\":\"response.done\",\"response_id\":\"private-original\",\"response\":{\"id\":\"private-original\",\"status\":\"cancelled\"}}":
        "{\"type\":\"response.done\",\"response\":{\"id\":\"private-original\",\"status\":\"cancelled\"}}";
    agent_err_t error=AGENT_OK;size_t length=strlen(wire);
    for(size_t i=0;i<length && !error;++i) {
        agent_ws_chunk_t part={.length=1,.opcode=i?0:1,.first=i==0,.final=i+1==length};
        error=agent_realtime_receive(&q,wire+i,&part);
    }
    if(!mode) {assert(!error && q.done && q.response_cancelled && !notices && !closed);return;}
    assert(error==AGENT_ERR_PROTOCOL && notices==1 && closed==1);
    cJSON *root=agent_json_parse(trace,strlen(trace));assert(root);
    assert(number(root,"before")== (mode==1?25u:17u));
    assert(number(root,"after")== (mode==3?20u:mode==1?25u:17u));
    assert(number(root,"current")==agent_crc32("private-original",16));
    assert(number(root,"id")==agent_crc32(mode==2?"private-other":"private-original",mode==2?13:16));
    assert(number(root,"top")== (mode==3?agent_crc32("private-original",16):0));
    assert(number(root,"segments")==2 && number(root,"revision")==2 && !number(root,"pending_revision"));
    cJSON_Delete(root);
}
int main(void)
{
    for(unsigned i=0;i<4;++i)check(i);
    puts("realtime rejection trace: normal cancel unchanged; odd PCM, wrong ID, terminal metadata distinct");
    return 0;
}
