#include "engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t flash[2*1024*1024];
static agent_engine_t engine;
static agent_context_t context;
static agent_core_t core;
static char wire[AGENT_HTTP_REQUEST_MAX+1];
static size_t used;
static unsigned scenario,requests,effects,rejections;
static uint64_t reserved;
static agent_err_t rd(void *p,size_t o,void *d,size_t n)
{(void)p;assert(o+n<=sizeof(flash));memcpy(d,flash+o,n);return AGENT_OK;}
static agent_err_t wr(void *p,size_t o,const void *d,size_t n)
{(void)p;const uint8_t *b=d;assert(o+n<=sizeof(flash));for(size_t i=0;i<n;++i){assert((flash[o+i]&b[i])==b[i]);flash[o+i]&=b[i];}return AGENT_OK;}
static agent_err_t erase(void *p,size_t o,size_t n)
{(void)p;assert(o+n<=sizeof(flash));memset(flash+o,255,n);return AGENT_OK;}
static agent_err_t reserve(void *p,uint64_t *first,uint64_t *last)
{(void)p;*first=reserved+1;reserved+=128;*last=reserved;return AGENT_OK;}
static uint64_t now(void *p){(void)p;return 1234;}
static agent_err_t light(void *p,const uint8_t rgb[3])
{(void)p;assert(rgb[0]==4);++effects;return AGENT_OK;}
static agent_err_t display_set(void *p,const agent_display_config_t *c)
{(void)p;assert(c->mode==AGENT_DISPLAY_CLOCK);++effects;return AGENT_OK;}
static agent_err_t display_status(void *p,char *out,size_t cap)
{(void)p;snprintf(out,cap,"{\"mode\":\"off\",\"ready\":false}");return AGENT_OK;}
static agent_err_t collect(void *p,const char *data,size_t n)
{(void)p;assert(used+n<sizeof(wire));memcpy(wire+used,data,n);used+=n;wire[used]=0;return AGENT_OK;}
static agent_err_t event(void *p,const agent_event_t *e)
{
    (void)p;
    if(e->type==AGENT_EVENT_TOOL_END && e->data && strstr(e->data,"\"executed\":false")) {
        ++rejections;if(scenario==6) agent_cancel(&core);
    }
    return AGENT_OK;
}
static void check_pairing(const cJSON *messages)
{
    const char *pending[4];unsigned count=0,at=0;
    const char *system=agent_json_string(messages->child,"content");
    assert(strstr(system,"Current-turn display snapshot") && strstr(system,"\"mode\":\"off\""));
    for(const cJSON *m=messages->child;m;m=m->next) {
        const cJSON *calls=cJSON_GetObjectItemCaseSensitive(m,"tool_calls");
        if(calls) {
            assert(at==count);count=at=0;
            for(const cJSON *c=calls->child;c;c=c->next) {assert(count<4);pending[count++]=agent_json_string(c,"id");}
        }
        if(!strcmp(agent_json_string(m,"role"),"tool")) {
            assert(at<count && !strcmp(pending[at++],agent_json_string(m,"tool_call_id")));
        }
    }
    assert(at==count);
}
static agent_err_t http(void *p,const agent_http_request_t *request,agent_http_feed_fn feed,void *ctx)
{
    (void)p;++requests;used=0;assert(!agent_http_write_body(request,collect,NULL));
    const char *end=NULL;cJSON *r=cJSON_ParseWithLengthOpts(wire,used,&end,false);
    assert(r && end==wire+used);check_pairing(cJSON_GetObjectItemCaseSensitive(r,"messages"));cJSON_Delete(r);
    if(requests==2) assert(!effects && strstr(wire,"executed") && strstr(wire,"false"));
    if(requests>=3 && scenario!=2) {
        assert(effects==(scenario==1?2u:1u));
        const char *done="{\"choices\":[{\"message\":{\"content\":\"done\"},\"finish_reason\":\"stop\"}]}";
        return feed(ctx,done,strlen(done));
    }
    bool invalid=requests==1 || scenario==2;
    char response[2048];agent_json_writer_t w;agent_json_writer_init(&w,response,sizeof(response));
    agent_json_raw(&w,"{\"choices\":[{\"message\":{\"content\":null,\"tool_calls\":[");
    unsigned calls=(scenario==1 || scenario==3)?2:1;
    for(unsigned i=0;i<calls;++i) {
        agent_json_printf(&w,"%s{\"id\":\"call-%u-%u\",\"type\":\"function\",\"function\":{\"name\":",i?",":"",requests,scenario==3?0:i);
        const char *name=scenario==4?"unknown_tool":i?"device_display_set":"device_light_set_rgb";
        const char *args=i?(invalid?"{\"mode\":\"clock\",\"utc_offset_minutes\":900}":"{\"mode\":\"clock\"}"):
            (invalid && scenario!=1?"{\"r\":300,\"g\":0,\"b\":0}":"{\"r\":4,\"g\":0,\"b\":0}");
        agent_json_quote(&w,name);agent_json_raw(&w,",\"arguments\":");agent_json_quote(&w,args);agent_json_raw(&w,"}}");
    }
    agent_json_printf(&w,"]},\"finish_reason\":\"%s\"}]}",scenario==5?"length":"tool_calls");assert(!w.error);
    return feed(ctx,response,w.used);
}
int main(void)
{
    const agent_flash_ops_t storage={rd,wr,erase,NULL,sizeof(flash),4096};
    const agent_display_ops_t display={display_set,display_status,NULL};
    const agent_tool_ops_t tools={.light_set=light,.now_ms=now,.display=&display};
    const agent_llm_ops_t llm={.parse=agent_llm_parse,.compose=agent_deepseek_compose};
    const agent_transport_ops_t transport={http,NULL};
    memset(flash,255,sizeof(flash));agent_wal_t wal;assert(!agent_wal_format(&wal,&storage));
    context.device="test";context.user="user";context.session="session";context.reserve=reserve;
    context.scratch=engine.buffer;context.capacity=sizeof(engine.buffer);assert(!agent_context_open(&context,&storage));
    engine.core=&core;engine.context=&context;engine.tools=&tools;engine.llm=&llm;engine.transport=&transport;engine.emit=event;
    for(scenario=0;scenario<7;++scenario) {
        requests=effects=rejections=0;agent_core_init(&core);assert(!agent_begin_turn(&core));
        agent_err_t e=agent_engine_turn(&engine,"Perform the requested action",false);agent_end_turn(&core);
        if(scenario<2) {assert(!e && requests==3 && effects==(scenario==1?2u:1u) && rejections==(scenario==1?2u:1u));}
        if(scenario==2) assert(e==AGENT_ERR_ARGUMENT && requests==3 && !effects && rejections==2);
        if(scenario==3) assert(e==AGENT_ERR_DUPLICATE && requests==1 && !effects && !rejections);
        if(scenario==4) assert(e==AGENT_ERR_TOOL && requests==1 && !effects && !rejections);
        if(scenario==5) assert(e==AGENT_ERR_LIMIT && requests==1 && !effects && !rejections);
        if(scenario==6) assert(e==AGENT_ERR_CANCELLED && requests==1 && !effects && rejections==1);
        assert(!context.prompt_locked);
    }
    puts("repair: live snapshot, bounded correction, atomic batch rejection, original IDs, no replay, malformed/unknown/truncated refusal, cancellation PASS");
}
