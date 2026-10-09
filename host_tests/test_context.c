#include "context.h"
#include "engine.h"
#include "sync.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char flash[131072];
static uint64_t reserved;
static long fail_write=-1;
static char scratch[AGENT_REQUEST_MAX+1],out[AGENT_REQUEST_MAX+1];
static agent_context_t context;
static agent_messages_t messages;
static agent_engine_storage_t engine_workspace;
static agent_engine_t engine={AGENT_ENGINE_STORAGE_INIT(engine_workspace)};
static agent_core_t core;
static unsigned requests,sets,emitted,pauses;
static int scenario;
static unsigned sync_requests;
static bool fail_pull,fail_ack;
static bool erase_busy;
static unsigned erases;
static uint64_t now_ms(void *ctx) { (void)ctx; return 1000; }
static void pause_ms(unsigned ms) { assert(ms==25); ++pauses; }
static agent_err_t output(void *ctx,const agent_event_t *event)
{ (void)ctx; if(event->type==AGENT_EVENT_TEXT) emitted+=(unsigned)event->length; return AGENT_OK; }
static agent_err_t light(void *ctx,const uint8_t rgb[3]) { (void)ctx; assert(rgb[0]==255); ++sets; return AGENT_OK; }
static agent_err_t mock_http(void *ctx,const agent_http_request_t *r,agent_http_feed_fn feed,void *feed_ctx)
{
    (void)ctx; ++requests;
    static char wire[AGENT_HTTP_REQUEST_MAX+1];
    agent_json_writer_t writer; agent_json_writer_init(&writer,wire,sizeof(wire));
    assert(!agent_http_write_body(r,agent_json_write_bytes,&writer));
    assert(writer.used==r->length);
    cJSON *request=cJSON_Parse(wire); assert(request);
    if(!scenario || scenario==6) assert(strstr(wire,"tool_call_id") || requests==1);
    if(scenario==6) {
        assert(!strcmp(agent_json_string(request,"schema"),"agent.turn/1"));
        cJSON *list=cJSON_GetObjectItemCaseSensitive(request,"messages");
        assert(cJSON_IsArray(list) && !strcmp(agent_json_string(list->child,"role"),"user"));
        assert(!strcmp(r->path,"/v1/agent/turns"));
        assert(cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(request,"capabilities")));
    }
    cJSON_Delete(request);
    if(scenario==1) return AGENT_ERR_AUTH;
    if(scenario==2 && requests==1) return AGENT_ERR_NETWORK;
    if(scenario==5) return AGENT_ERR_NETWORK;
    if(scenario==3) {
        const char *chunk="data: {\"choices\":[{\"delta\":{\"content\":\"partial\"}}]}\n\n";
        assert(!feed(feed_ctx,chunk,strlen(chunk))); return AGENT_ERR_NETWORK;
    }
    if(((!scenario || scenario==6) && requests==1) || scenario==4) {
        const char *tool="{\"choices\":[{\"message\":{\"content\":null,\"tool_calls\":[{\"id\":\"call-original\",\"type\":\"function\",\"function\":{\"name\":\"device_light_set_rgb\",\"arguments\":\"{\\\"r\\\":255,\\\"g\\\":0,\\\"b\\\":0}\"}}]},\"finish_reason\":\"tool_calls\"}]}";
        return feed(feed_ctx,tool,strlen(tool));
    }
    const char *reply="{\"choices\":[{\"message\":{\"content\":\"OK\"},\"finish_reason\":\"stop\"}]}";
    return feed(feed_ctx,reply,strlen(reply));
}
static agent_err_t rd(void *ctx,size_t o,void *p,size_t n) { (void)ctx; assert(o+n<=sizeof(flash)); memcpy(p,flash+o,n); return AGENT_OK; }
static agent_err_t wr(void *ctx,size_t o,const void *p,size_t n)
{
    (void)ctx; const unsigned char *s=p; assert(o+n<=sizeof(flash));
    for(size_t i=0;i<n;++i) { if(!fail_write) return AGENT_ERR_STORAGE; if(fail_write>0) --fail_write; assert((flash[o+i]&s[i])==s[i]); flash[o+i]&=s[i]; }
    return AGENT_OK;
}
static agent_err_t er(void *ctx,size_t o,size_t n) { (void)ctx; ++erases;if(erase_busy) return AGENT_ERR_BUSY; assert(o%4096==0 && n%4096==0 && o+n<=sizeof(flash)); memset(flash+o,255,n); return AGENT_OK; }
static agent_err_t reserve(void *ctx,uint64_t *first,uint64_t *last)
{ (void)ctx; *first=reserved+1; reserved+=128; *last=reserved; return AGENT_OK; }
static const agent_flash_ops_t ops={rd,wr,er,NULL,sizeof(flash),4096};
static void boot(void)
{
    memset(&context,0,sizeof(context)); context.device="device-a"; context.user="user"; context.session="session";
    context.reserve=reserve; context.scratch=scratch; context.capacity=sizeof(scratch);
    assert(!agent_context_open(&context,&ops));
}
static cJSON *remote(const char *device,unsigned seq,unsigned lamport,const char *type,const char *content)
{
    snprintf(out,sizeof(out),"{\"schema\":\"agent.context.event/1\",\"event_id\":\"%s:%020u\",\"device_id\":\"%s\",\"user_id\":\"user\",\"session_id\":\"session\",\"device_seq\":%u,\"lamport\":%u,\"type\":\"%s\",\"actor\":{},\"content\":%s,\"policy\":{},\"parents\":[]}",device,seq,device,seq,lamport,type,content);
    cJSON *root=agent_json_parse(out,strlen(out)); assert(root); return root;
}
static agent_err_t sync_http(void *ctx,const agent_http_request_t *r,agent_http_feed_fn feed,void *feed_ctx)
{
    (void)ctx; ++sync_requests; char response[2048];
    if(!strcmp(r->method,"GET")) {
        assert(strstr(r->path,"user_id=user") && strstr(r->path,"session_id=session"));
        if(strstr(r->path,"cursor=0&")) {
            cJSON *event=remote("syncpeer",1,1000,"memory","{\"key\":\"color\",\"value\":\"cyan\"}");
            char *text=cJSON_PrintUnformatted(event); assert(text);
            snprintf(response,sizeof(response),"{\"cursor\":1,\"events\":[%s],\"more\":false}",text);
            free(text); cJSON_Delete(event);
        } else strcpy(response,"{\"cursor\":1,\"events\":[],\"more\":false}");
        if(fail_pull) { fail_write=0; fail_pull=false; }
    } else {
        cJSON *body=agent_json_parse(r->body,r->length); assert(body);
        if(strstr(r->path,"events:batch")) {
            cJSON *events=cJSON_GetObjectItemCaseSensitive(body,"events"),*last=events->child;
            assert(last); while(last->next) last=last->next;
            uint64_t seq; assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(last,"device_seq"),AGENT_SEQ_MAX,&seq));
            snprintf(response,sizeof(response),"{\"accepted\":2,\"acked_seq\":%llu}",(unsigned long long)seq);
        } else {
            assert(strstr(r->path,"/ack"));
            if(fail_ack) { fail_ack=false; cJSON_Delete(body); return AGENT_ERR_NETWORK; }
            strcpy(response,"{\"ok\":true,\"cursor\":1}");
        }
        cJSON_Delete(body);
    }
    return feed(feed_ctx,response,strlen(response));
}
int main(void)
{
    memset(flash,255,sizeof(flash)); agent_wal_t wal; assert(!agent_wal_format(&wal,&ops)); boot();
    char first_id[64],next_id[64];
    assert(!agent_context_emit(&context,"message","user","{\"text\":\"hello\"}",first_id,sizeof(first_id)));
    assert(context.pending==1 && context.lamport==1);
    boot(); assert(context.pending==1);
    assert(!agent_context_emit(&context,"message","user","{\"text\":\"second\"}",next_id,sizeof(next_id)));
    assert(strcmp(first_id,next_id) && context.last_local==129);
    cJSON *r=remote("phone",3,10,"memory","{\"key\":\"color\",\"value\":\"red\"}");
    assert(!agent_context_ingest(&context,r)); size_t events=context.events;
    assert(!agent_context_ingest(&context,r) && context.events==events); cJSON_Delete(r);
    r=remote("phone",1,7,"memory","{\"key\":\"color\",\"value\":\"blue\"}");
    assert(!agent_context_ingest(&context,r)); cJSON_Delete(r); assert(!strcmp(context.memory[0].value,"red"));
    r=remote("phone",2,9,"memory","{\"key\":\"color\",\"value\":\"green\"}");
    assert(!agent_context_ingest(&context,r)); cJSON_Delete(r);
    assert(context.peers[1].count==1 && context.peers[1].ranges[0].first==1 && context.peers[1].ranges[0].last==3);
    r=remote("tablet",1,10,"tombstone","{\"key\":\"color\"}");
    assert(!agent_context_ingest(&context,r)); cJSON_Delete(r); assert(context.memory[0].deleted);
    r=remote("phone",4,10,"memory","{\"key\":\"color\",\"value\":\"purple\"}");
    assert(!agent_context_ingest(&context,r)); cJSON_Delete(r); assert(context.memory[0].deleted);
    const char *turn="{\"messages\":[{\"role\":\"user\",\"content\":\"remember test\"},{\"role\":\"assistant\",\"content\":\"remembered\"}]}";
    assert(!agent_context_emit(&context,"turn","assistant",turn,NULL,0));
    assert(!agent_context_history(&context,&messages,"system")); assert(strstr(messages.data,"remembered"));
    uint64_t last; assert(!agent_context_batch(&context,out,sizeof(out),&last)); assert(last==129);
    last=context.last_local;
    uint64_t old_cursor=context.cursor; fail_write=10;
    assert(agent_context_checkpoint(&context,7,last,AGENT_CONTEXT_CLOUD)==AGENT_ERR_STORAGE);
    assert(context.cursor==old_cursor && context.pending==3); fail_write=-1; boot();
    assert(context.wal.tail_recovered && context.pending==3);
    assert(!agent_context_checkpoint(&context,7,last,AGENT_CONTEXT_CLOUD));
    assert(context.pending==0); assert(!agent_context_compact(&context)); boot();
    assert(context.cursor==7 && context.mode==AGENT_CONTEXT_CLOUD && context.pending==0 && context.memory[0].deleted);
    assert(!agent_context_history(&context,&messages,"system") && strstr(messages.data,"remembered"));
    r=remote("phone",1,7,"memory","{\"key\":\"color\",\"value\":\"blue\"}");
    events=context.events; assert(!agent_context_ingest(&context,r) && context.events==events); cJSON_Delete(r);
    assert(!agent_context_emit(&context,"message","user","{\"text\":\"after restart\"}",next_id,sizeof(next_id)));
    assert(context.last_local>last && context.pending==1);
    agent_core_init(&core);
    static const agent_transport_ops_t transport={mock_http,NULL};
    static const agent_llm_ops_t llm={.build=agent_deepseek_request,.parse=agent_llm_parse,.compose=agent_deepseek_compose};
    static const agent_tool_ops_t tools={.light_set=light,.now_ms=now_ms};
    engine.core=&core; engine.transport=&transport; engine.llm=&llm; engine.tools=&tools;
    engine.context=&context; engine.emit=output; engine.pause_ms=pause_ms;
    for(scenario=0;scenario<=6;++scenario) {
        requests=sets=emitted=pauses=0; engine.failures=0; engine.circuit_until=0;
        engine.gateway=scenario==6;
        assert(!agent_begin_turn(&core));
        agent_err_t result=agent_engine_turn(&engine,"test",scenario==3);
        agent_end_turn(&core);
        if(scenario==0) { assert(!result && requests==2 && sets==1); }
        if(scenario==1) { assert(result==AGENT_ERR_AUTH && requests==1 && !sets); }
        if(scenario==2) { assert(!result && requests==2 && pauses && !sets); }
        if(scenario==3) { assert(result==AGENT_ERR_NETWORK && requests==1 && emitted==7); }
        if(scenario==4) { assert(result==AGENT_ERR_LIMIT && requests==5 && sets==4); }
        if(scenario==5) {
            assert(result==AGENT_ERR_NETWORK && requests==2);
            for(unsigned i=0;i<2;++i) {
                assert(!agent_begin_turn(&core)); assert(agent_engine_turn(&engine,"test",false)==AGENT_ERR_NETWORK); agent_end_turn(&core);
            }
            unsigned before=requests;
            assert(!agent_begin_turn(&core)); assert(agent_engine_turn(&engine,"test",false)==AGENT_ERR_BUSY); agent_end_turn(&core);
            assert(requests==before && engine.circuit_until==31000);
        }
        if(scenario==6) assert(!result && requests==2 && sets==1);
    }
    /* A separate clean store tests cursor durability rather than an already advanced cursor. */
    assert(!agent_wal_format(&wal,&ops)); boot();
    assert(!agent_context_emit(&context,"message","user","{\"text\":\"pending\"}",NULL,0));
    const agent_transport_ops_t sync_transport={sync_http,NULL}; bool more;
    fail_pull=true;
    assert(agent_context_sync(&context,&sync_transport,NULL,&more)==AGENT_ERR_STORAGE);
    assert(context.cursor==0 && context.pending==0); fail_write=-1;
    fail_ack=true;
    assert(agent_context_sync(&context,&sync_transport,NULL,&more)==AGENT_ERR_NETWORK);
    assert(context.cursor==1 && !strcmp(context.memory[0].value,"cyan"));
    boot(); size_t before=context.events;
    assert(!agent_context_sync(&context,&sync_transport,NULL,&more) && context.events==before && !more);
    assert(!agent_context_checkpoint(&context,context.cursor,context.acked,AGENT_CONTEXT_LOCAL));
    unsigned count=sync_requests;
    assert(agent_context_sync(&context,&sync_transport,NULL,&more)==AGENT_ERR_CONFIG && count==sync_requests);
    /* More reboots than the exact remote-range budget must not fill local context. */
    assert(!agent_wal_format(&wal,&ops)); boot();
    for(unsigned i=0;i<40;++i) {
        uint64_t prior=context.last_local;
        assert(!agent_context_emit(&context,"message","user","{\"text\":\"reboot\"}",NULL,0));
        assert(context.last_local>prior && context.peers[0].count==1);
        if(i==20) assert(!agent_context_compact(&context));
        boot(); assert(context.events==i+1 && context.pending==i+1);
    }
    before=context.events;
    r=remote("device-a",(unsigned)context.last_local,1,"message","{\"text\":\"echo\"}");
    assert(!agent_context_ingest(&context,r) && context.events==before); cJSON_Delete(r);
    r=remote("device-a",(unsigned)reserved+1,1,"message","{\"text\":\"foreign allocation\"}");
    assert(agent_context_ingest(&context,r)==AGENT_ERR_FORBIDDEN); cJSON_Delete(r);
    /* Already-full old local snapshots can advance without dropping any event. */
    agent_peer_t *own=&context.peers[0]; own->count=AGENT_RANGES_MAX;
    for(unsigned i=0;i<AGENT_RANGES_MAX;++i) own->ranges[i]=(agent_range_t){1+i*128,1+i*128};
    assert(!agent_context_emit(&context,"message","user","{\"text\":\"upgrade\"}",NULL,0));
    assert(own->count==1 && context.events==before+1);
    /* Near-full storage must not erase while a small result still fits. Audio
     * refuses erases while playing; this used to turn successful music into busy. */
    assert(!agent_wal_format(&wal,&ops));boot();
    while(context.wal.bank_size-context.wal.used>=AGENT_REQUEST_MAX)
        assert(!agent_context_emit(&context,"message","user","{\"text\":\"retained\"}",NULL,0));
    unsigned erased=erases;before=context.events;uint64_t generation=context.wal.generation;
    erase_busy=true;
    assert(!agent_context_emit(&context,"turn","assistant",turn,NULL,0));
    assert(erases==erased && context.events==before+1 && context.wal.generation==generation);
    erase_busy=false;boot();assert(context.events==before+1);
    /* A genuinely full bank reports full and retains every committed event. */
    agent_err_t full;
    do {before=context.events;full=agent_context_emit(&context,"message","user","{\"text\":\"retained\"}",NULL,0);} while(!full);
    assert(full==AGENT_ERR_FULL);boot();assert(context.events==before);
    puts("durable IDs, 40-reboot local dedup, out-of-order remote dedup, LWW, tombstones and recovery passed");
}
