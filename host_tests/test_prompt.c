#include "engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char flash[2*1024*1024];
static agent_engine_t engine;
static agent_context_t context;
static agent_core_t core;
static char wire[AGENT_HTTP_REQUEST_MAX+1], saved[AGENT_HTTP_REQUEST_MAX+1];
static size_t written, peak_chunk;
static uint64_t reserved;
static unsigned requests, scenario, effects;
static uint64_t scan_clock;
static uint64_t slow_clock(void *ctx) { (void)ctx; scan_clock+=100; return scan_clock; }

static agent_err_t rd(void *ctx,size_t off,void *data,size_t n)
{ (void)ctx; assert(off+n<=sizeof(flash)); memcpy(data,flash+off,n); return AGENT_OK; }
static agent_err_t wr(void *ctx,size_t off,const void *data,size_t n)
{
    (void)ctx; const unsigned char *p=data; assert(off+n<=sizeof(flash));
    for(size_t i=0;i<n;++i) { assert((flash[off+i]&p[i])==p[i]); flash[off+i]&=p[i]; }
    return AGENT_OK;
}
static agent_err_t er(void *ctx,size_t off,size_t n)
{ (void)ctx; assert(!(off%4096) && !(n%4096) && off+n<=sizeof(flash)); memset(flash+off,255,n); return AGENT_OK; }
static const agent_flash_ops_t storage={rd,wr,er,NULL,sizeof(flash),4096};
static agent_err_t reserve(void *ctx,uint64_t *first,uint64_t *last)
{ (void)ctx; *first=reserved+1; reserved+=128; *last=reserved; return AGENT_OK; }
static void boot(void)
{
    memset(&context,0,sizeof(context));
    context.device="prompt-test"; context.user="user"; context.session="session";
    context.reserve=reserve; context.scratch=engine.buffer; context.capacity=sizeof(engine.buffer);
    assert(!agent_context_open(&context,&storage));
}
static uint64_t now(void *ctx) { (void)ctx; return 1000; }
static void pause_ms(unsigned ms) { (void)ms; }
static agent_err_t light(void *ctx,const uint8_t rgb[3])
{ (void)ctx; assert(rgb[0]==4); ++effects; return AGENT_OK; }
static agent_err_t collect(void *ctx,const char *data,size_t n)
{
    (void)ctx; assert(n<=4096 && written+n<sizeof(wire));
    if(n>peak_chunk) peak_chunk=n;
    memcpy(wire+written,data,n); written+=n; wire[written]=0;
    if(scenario==3) atomic_store(&core.cancelled,true);
    return AGENT_OK;
}
static void check_messages(const cJSON *root)
{
    const cJSON *messages=cJSON_GetObjectItemCaseSensitive(root,"messages");
    assert(cJSON_IsArray(messages) && cJSON_GetArraySize(messages)>64);
    assert(!strcmp(agent_json_string(messages->child,"role"),"system"));
    assert(strstr(agent_json_string(messages->child,"content"),"CURRENT_BOARD_LCD MOSI4 SCLK5 DC10"));
    bool current=false;
    const char *pending=NULL;
    for(const cJSON *m=messages->child;m;m=m->next) {
        const char *role=agent_json_string(m,"role"); assert(role);
        const char *text=agent_json_string(m,"content");
        if(text && !strcmp(text,"CURRENT_QUERY")) current=true;
        if(text && strstr(text,"ANCHOR_")) assert(!current); /* Older history precedes this turn. */
        const cJSON *calls=cJSON_GetObjectItemCaseSensitive(m,"tool_calls");
        if(calls) { assert(!pending); pending=agent_json_string(calls->child,"id"); assert(pending); }
        if(!strcmp(role,"tool")) { assert(pending && !strcmp(pending,agent_json_string(m,"tool_call_id"))); pending=NULL; }
    }
    assert(!pending && current);
}
static agent_err_t http(void *ctx,const agent_http_request_t *r,agent_http_feed_fn feed,void *feed_ctx)
{
    (void)ctx; ++requests; written=peak_chunk=0;
    assert(r->produce && !r->body && r->length>AGENT_REQUEST_MAX && context.prompt_locked);
    assert(agent_context_compact(&context)==AGENT_ERR_BUSY);
    assert(agent_context_emit(&context,"message","user","{}",NULL,0)==AGENT_ERR_BUSY);
    size_t bad=context.recent[context.prompt_start].offset+32;
    if(scenario==4) flash[bad]^=1;
    agent_err_t error=agent_http_write_body(r,collect,NULL);
    if(scenario==4) { flash[bad]^=1; assert(error==AGENT_ERR_CORRUPT); return error; }
    if(scenario==3) { assert(error==AGENT_ERR_CANCELLED); return error; }
    assert(!error && written==r->length && peak_chunk<=4096);
    const char *end=NULL;
    cJSON *root=cJSON_ParseWithLengthOpts(wire,written,&end,false);
    assert(root && end==wire+written); check_messages(root); cJSON_Delete(root);
    assert(strstr(wire,"CURRENT_QUERY") && strstr(wire,"记忆") && strstr(wire,"ANCHOR_159"));
    if(scenario==1) {
        if(requests==1) { memcpy(saved,wire,written+1); return AGENT_ERR_NETWORK; }
        assert(requests==2 && !strcmp(saved,wire));
    }
    if(scenario==2 && requests==1) {
        const char *tool="{\"choices\":[{\"message\":{\"content\":null,\"tool_calls\":[{\"id\":\"original-id\",\"type\":\"function\",\"function\":{\"name\":\"device_light_set_rgb\",\"arguments\":\"{\\\"r\\\":4,\\\"g\\\":2,\\\"b\\\":1}\"}}]},\"finish_reason\":\"tool_calls\"}]}";
        return feed(feed_ctx,tool,strlen(tool));
    }
    if(scenario==2) assert(strstr(wire,"\"tool_call_id\":\"original-id\""));
    if(scenario==5) {
        const char *partial="data: {\"choices\":[{\"delta\":{\"content\":\"partial\"}}]}\n\n";
        assert(!feed(feed_ctx,partial,strlen(partial))); return AGENT_ERR_NETWORK;
    }
    const char *reply="{\"choices\":[{\"message\":{\"content\":\"OK\"},\"finish_reason\":\"stop\"}]}";
    return feed(feed_ctx,reply,strlen(reply));
}
static agent_err_t tiny(void *ctx,agent_write_fn write,void *write_ctx)
{ (void)ctx; return write(write_ctx,"abc",3); }
static agent_err_t bad_producer(void *ctx,agent_write_fn write,void *write_ctx)
{ (void)ctx; (void)write(write_ctx,"1234",4); return AGENT_OK; }
static void scratch_cycle(void)
{
    unsigned char prefix[offsetof(agent_engine_t,messages)];
    unsigned char suffix[sizeof(engine)-offsetof(agent_engine_t,used)];
    memcpy(prefix,&engine,sizeof(prefix)); memcpy(suffix,&engine.used,sizeof(suffix));
    size_t capacity; void *memory=agent_engine_scratch(&engine,&capacity);
    /* A completed borrow destroys all prior messages and parser state. The
     * following real HTTP/history/tool scenarios must initialize them anew. */
    memset(memory,0xa5,capacity); memset(memory,0,capacity);
    assert(!memcmp(prefix,&engine,sizeof(prefix)) && !memcmp(suffix,&engine.used,sizeof(suffix)));
}

int main(void)
{
    memset(flash,255,sizeof(flash)); agent_wal_t wal; assert(!agent_wal_format(&wal,&storage)); boot();
    char pad[1101],turn[4096]; memset(pad,'x',sizeof(pad)-1); pad[sizeof(pad)-1]=0;
    for(unsigned i=0;i<160;++i) {
        if(i%7==0) snprintf(turn,sizeof(turn),"{\"messages\":[{\"role\":\"user\",\"content\":\"ANCHOR_%03u 记忆 %s\"},{\"role\":\"assistant\",\"content\":null,\"tool_calls\":[{\"id\":\"hist-%u\",\"type\":\"function\",\"function\":{\"name\":\"device_status_get\",\"arguments\":\"{}\"}}]},{\"role\":\"tool\",\"tool_call_id\":\"hist-%u\",\"content\":\"{}\"},{\"role\":\"assistant\",\"content\":\"记住了\"}]}",i,pad,i,i);
        else snprintf(turn,sizeof(turn),"{\"messages\":[{\"role\":\"user\",\"content\":\"ANCHOR_%03u 记忆 %s\"},{\"role\":\"assistant\",\"content\":\"记住了\"}]}",i,pad);
        assert(!agent_context_emit(&context,"turn","assistant",turn,NULL,0));
    }
    assert(context.recent_count==128);
    size_t events=context.events;
    static const agent_transport_ops_t transport={http,NULL};
    static const agent_llm_ops_t llm={.build=agent_deepseek_request,.parse=agent_llm_parse,.compose=agent_deepseek_compose};
    static const agent_tool_ops_t tools={.light_set=light,.now_ms=now,
        .hardware_prompt="CURRENT_BOARD_LCD MOSI4 SCLK5 DC10"};
    engine.core=&core; engine.context=&context; engine.transport=&transport; engine.llm=&llm; engine.tools=&tools;
    engine.pause_ms=pause_ms;
    for(scenario=0;scenario<=5;++scenario) {
        scratch_cycle();
        requests=effects=0; engine.failures=0; agent_core_init(&core); assert(!agent_begin_turn(&core));
        agent_err_t error=agent_engine_turn(&engine,"CURRENT_QUERY",scenario==5); agent_end_turn(&core);
        assert(!context.prompt_locked && context.prompt_bytes<=65536 && context.prompt_count>32);
        if(scenario<=2) assert(!error);
        if(scenario==1) assert(requests==2 && !effects);
        if(scenario==2) assert(requests==2 && effects==1);
        if(scenario==3) assert(error==AGENT_ERR_CANCELLED && requests==1 && !effects);
        if(scenario==4) assert(error==AGENT_ERR_CORRUPT && requests==1 && !effects);
        if(scenario==5) assert(error==AGENT_ERR_NETWORK && requests==1 && engine.output_started);
    }
    unsigned count64=context.prompt_count;
    context.history_budget=AGENT_CONTEXT_BUDGET_MAX;
    assert(!agent_context_checkpoint(&context,context.cursor,context.acked,context.mode));
    boot(); assert(context.history_budget==AGENT_CONTEXT_BUDGET_MAX && context.recent_count==128);
    scratch_cycle();
    scenario=0; requests=0; agent_core_init(&core); assert(!agent_begin_turn(&core));
    assert(!agent_engine_turn(&engine,"CURRENT_QUERY",false)); agent_end_turn(&core);
    assert(context.prompt_count>count64 && context.prompt_bytes>120*1024 && context.prompt_bytes<=128*1024);
    assert(context.events>events && context.pending==context.events);
    size_t pending=context.pending; assert(!agent_context_compact(&context)); boot();
    assert(context.pending==pending && context.recent_count==128);
    /* Recall reaches older retained turns outside the 128-entry prompt index. */
    char recalled[4096];
    assert(!agent_context_search(&context,"anchor_000",0,3,recalled,sizeof(recalled)));
    cJSON *found=agent_json_parse(recalled,strlen(recalled)); assert(found);
    cJSON *hits=cJSON_GetObjectItemCaseSensitive(found,"hits");
    assert(cJSON_GetArraySize(hits)==1 && strstr(agent_json_string(hits->child,"excerpt"),"ANCHOR_000"));
    char event_id[64]; strcpy(event_id,agent_json_string(hits->child,"event_id")); cJSON_Delete(found);
    assert(!agent_context_search(&context,event_id,0,1,recalled,sizeof(recalled)));
    assert(strstr(recalled,event_id));
    assert(!agent_context_search(&context,"记忆",0,2,recalled,sizeof(recalled)));
    found=agent_json_parse(recalled,strlen(recalled)); assert(found);
    hits=cJSON_GetObjectItemCaseSensitive(found,"hits"); assert(cJSON_GetArraySize(hits)==2);
    uint64_t before; assert(agent_json_uint(cJSON_GetObjectItemCaseSensitive(found,"next_before"),AGENT_SEQ_MAX,&before));
    strcpy(event_id,agent_json_string(hits->child,"event_id"));
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(found,"more"))); cJSON_Delete(found);
    assert(!agent_context_search(&context,"记忆",before,2,recalled,sizeof(recalled)) && !strstr(recalled,event_id));
    assert(!agent_context_search(&context,"no_such_phrase",0,3,recalled,sizeof(recalled)) && strstr(recalled,"\"hits\":[]"));
    size_t prior=context.events;
    context.now_ms=slow_clock;
    assert(agent_context_search(&context,"anchor_000",0,3,recalled,sizeof(recalled))==AGENT_ERR_TIMEOUT);
    assert(agent_context_summary_set(&context,"timed out",0)==AGENT_ERR_TIMEOUT && context.events==prior);
    atomic_bool scan_cancel=true; context.cancelled=&scan_cancel;
    assert(agent_context_search(&context,"anchor_000",0,3,recalled,sizeof(recalled))==AGENT_ERR_CANCELLED);
    assert(agent_context_summary_set(&context,"cancelled",0)==AGENT_ERR_CANCELLED && context.events==prior);
    context.now_ms=NULL; context.cancelled=NULL;
    assert(agent_context_summary_set(&context,"bad source",context.last_local+1)==AGENT_ERR_NOT_FOUND && context.events==prior);
    uint64_t boundary=context.last_turn_seq;
    assert(!agent_context_summary_set(&context,"最早的 ANCHOR_000 要保留，未完成工作仍需核对。",0));
    assert(context.summary.through==boundary);
    assert(!agent_context_compact(&context)); boot();
    assert(context.summary.through==boundary && strstr(context.summary.text,"ANCHOR_000"));
    assert(!agent_context_prompt(&context,&engine.messages,"system") && strstr(engine.messages.data,"ANCHOR_000"));
    assert(!agent_context_summary_get(&context,recalled,sizeof(recalled)) && strstr(recalled,"through_event_id"));
    /* Escaping must not produce invalid JSON when a small status buffer truncates a summary. */
    memset(context.summary.text,'"',AGENT_SUMMARY_MAX); context.summary.text[AGENT_SUMMARY_MAX]=0;
    assert(!agent_context_summary_get(&context,recalled,256));
    found=agent_json_parse(recalled,strlen(recalled)); assert(found && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(found,"truncated"))); cJSON_Delete(found);
    atomic_bool cancelled=false;
    assert(!agent_context_select(&context,context.wal.next_seq,&cancelled));
    atomic_store(&cancelled,true); assert(agent_context_replay(&context,collect,NULL)==AGENT_ERR_CANCELLED);
    agent_context_release(&context);
    agent_http_request_t r={.produce=tiny,.length=4}; written=0;
    assert(agent_http_write_body(&r,collect,NULL)==AGENT_ERR_PROTOCOL);
    r.produce=bad_producer; r.length=3;
    assert(agent_http_write_body(&r,collect,NULL)==AGENT_ERR_LIMIT);
    r.body="abc"; assert(agent_http_write_body(&r,collect,NULL)==AGENT_ERR_ARGUMENT);
    size_t length; assert(agent_http_measure_body(tiny,NULL,&cancelled,&length)==AGENT_ERR_CANCELLED);
    puts("64/128 KiB Flash prompts, source recall, durable summary, exact framing, tool IDs, retry, cancellation, CRC, compaction lock and reboot PASS");
}
