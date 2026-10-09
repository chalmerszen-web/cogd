#include "engine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char flash[2*1024*1024];
static unsigned char prior_flash[sizeof(flash)];
static agent_engine_storage_t engine_workspace;
static agent_engine_storage_t alternate_workspace;
static agent_engine_t engine={AGENT_ENGINE_STORAGE_INIT(engine_workspace)};
static agent_context_t context;
static agent_core_t core;
static char wire[AGENT_HTTP_REQUEST_MAX+1], saved[AGENT_HTTP_REQUEST_MAX+1];
static size_t written, peak_chunk;
static uint64_t reserved;
static unsigned requests, scenario, effects;
static unsigned flash_reads,optimized_reads;
static uint64_t scan_clock;
static uint64_t uptime=1000;
static size_t cache_prefix;
static uint64_t slow_clock(void *ctx) { (void)ctx; scan_clock+=100; return scan_clock; }
static uint64_t expired_clock(void *ctx) { (void)ctx; scan_clock+=AGENT_CONTEXT_SCAN_MS+1; return scan_clock; }

static agent_err_t rd(void *ctx,size_t off,void *data,size_t n)
{ (void)ctx; ++flash_reads; assert(off+n<=sizeof(flash)); memcpy(data,flash+off,n); return AGENT_OK; }
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
    context.reserve=reserve; context.scratch=engine.buffer; context.capacity=AGENT_ENGINE_BUFFER_SIZE;
    assert(!agent_context_open(&context,&storage));
}
static uint64_t now(void *ctx) { (void)ctx; return uptime; }
static agent_err_t display_status(void *ctx,char *out,size_t cap)
{ (void)ctx;snprintf(out,cap,"{\"mode\":\"clock\",\"ready\":true}");return AGENT_OK; }
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
    assert(!strstr(agent_json_string(messages->child,"content"),"Current-turn display snapshot"));
    bool current=false;
    unsigned snapshots=0;
    const char *pending=NULL;
    for(const cJSON *m=messages->child;m;m=m->next) {
        const char *role=agent_json_string(m,"role"); assert(role);
        const char *text=agent_json_string(m,"content");
        if(text && strstr(text,"Current-turn display snapshot")) {
            assert(!strcmp(role,"system") && strstr(text,"\"mode\":\"clock\""));
            assert(m->next && !strcmp(agent_json_string(m->next,"content"),"CURRENT_QUERY"));
            ++snapshots;
        }
        if(text && !strcmp(text,"CURRENT_QUERY")) current=true;
        if(text && strstr(text,"ANCHOR_")) assert(!current && !snapshots); /* History precedes live state and this turn. */
        const cJSON *calls=cJSON_GetObjectItemCaseSensitive(m,"tool_calls");
        if(calls) { assert(!pending); pending=agent_json_string(calls->child,"id"); assert(pending); }
        if(!strcmp(role,"tool")) { assert(pending && !strcmp(pending,agent_json_string(m,"tool_call_id"))); pending=NULL; }
    }
    assert(!pending && current && snapshots==1);
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
    /* Optional deterministic wire evidence, collected before production
     * edits as well as afterward. No cloud traffic is involved in this test. */
    const char *capture=getenv("AGENT_REQUEST_CAPTURE_DIR");
    if(capture) {
        static unsigned sequence;
        char path[768];
        int n=snprintf(path,sizeof(path),"%s/%03u-scenario-%u-request-%u.json",capture,++sequence,scenario,requests);
        assert(n>0 && (size_t)n<sizeof(path));
        FILE *f=fopen(path,"wb");assert(f);
        assert(fwrite(wire,1,written,f)==written);assert(!fclose(f));
    }
    const char *end=NULL;
    cJSON *root=cJSON_ParseWithLengthOpts(wire,written,&end,false);
    assert(root && end==wire+written); check_messages(root); cJSON_Delete(root);
    assert(strstr(wire,"CURRENT_QUERY") && strstr(wire,"记忆") && strstr(wire,"ANCHOR_159"));
    const char *snapshot=strstr(wire,"Current-turn display snapshot");assert(snapshot);
    assert(strstr(engine.workspace->messages.data,"Current-turn display snapshot")>=engine.workspace->messages.data+engine.workspace->messages.system_used);
    if(scenario==6) {
        assert(strstr(snapshot,"uptime 1000 ms"));
        cache_prefix=(size_t)(snapshot-wire);assert(cache_prefix>190*1024);
        memcpy(saved,wire,cache_prefix);
    }
    if(scenario==7) {
        assert(strstr(snapshot,"uptime 2000 ms"));
        assert((size_t)(snapshot-wire)==cache_prefix && !memcmp(saved,wire,cache_prefix));
    }
    if(scenario==8) memcpy(saved,wire,written+1);
    if(scenario==9 || scenario==10) assert(!strcmp(saved,wire));
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
static agent_err_t no_snapshot(void *ctx,const agent_record_t *record,const char *data)
{ (void)ctx;(void)record;assert(!strstr(data,"Current-turn display snapshot"));return AGENT_OK; }
static agent_err_t custom_replay(agent_context_t *c,agent_write_fn write,void *ctx)
{ return agent_context_replay(c,write,ctx); }
static agent_err_t custom_compose(bool stream,agent_body_fn messages,void *messages_ctx,
                                 agent_body_fn tools,void *tools_ctx,agent_write_fn write,void *ctx)
{ return agent_deepseek_compose(stream,messages,messages_ctx,tools,tools_ctx,write,ctx); }
static void scratch_cycle(void)
{
    /* Relocate to a different live allocation before actual long-history HTTP
     * turns below; the context writer must follow it, not the prior arena. */
    agent_engine_storage_t *next=engine.workspace==&engine_workspace.work?&alternate_workspace:&engine_workspace;
    assert(!agent_engine_bind_workspace(&engine,NULL,0));
    if(next==&alternate_workspace)
        assert(!agent_engine_bind_parts(&engine,&next->work,sizeof(next->work),next->buffer,sizeof(next->buffer)));
    else assert(!agent_engine_bind_workspace(&engine,next,sizeof(*next)));
    unsigned char state[sizeof(engine)];
    memcpy(state,&engine,sizeof(state));
    size_t capacity; void *memory=agent_engine_scratch(&engine,&capacity);
    /* A completed borrow destroys all prior messages and parser state. The
     * following real HTTP/history/tool scenarios must initialize them anew. */
    memset(memory,0xa5,capacity); memset(memory,0,capacity);
    assert(!memcmp(state,&engine,sizeof(state)));
}
static void recall_answer_checks(void)
{
    memset(flash,255,sizeof(flash));agent_wal_t wal;
    assert(!agent_wal_format(&wal,&storage));boot();
    const char *content="{\"messages\":[{\"role\":\"user\",\"content\":\"灯的名字是什么？\"},"
        "{\"role\":\"assistant\",\"content\":\"正在查询\"},{\"role\":\"tool\",\"content\":\"never_emit_tool\"},"
        "{\"role\":\"assistant\",\"content\":\"叫小星星。\"},{\"role\":\"user\",\"content\":\"另一个问题\"},"
        "{\"role\":\"assistant\",\"content\":\"never_emit_later\"}]}";
    assert(!agent_context_emit(&context,"turn","assistant",content,NULL,0));
    char output[1024];
    assert(!agent_context_search(&context,"名字",0,1,output,sizeof(output)));
    assert(strstr(output,"叫小星星") && strstr(output,"assistant:") && !strstr(output,"正在查询") &&
           !strstr(output,"never_emit"));
    assert(!agent_context_search(&context,"叫小星星",0,1,output,sizeof(output)));
    assert(strstr(output,"叫小星星") && !strstr(output,"assistant:") && !strstr(output,"never_emit"));
    char long_text[1600];size_t n=0;
    for(unsigned i=0;i<180;++i) {memcpy(long_text+n,"边",3);n+=3;}
    strcpy(long_text+n,"边界\"\\\n");
    char turn[4096];agent_json_writer_t w;agent_json_writer_init(&w,turn,sizeof(turn));
    agent_json_raw(&w,"{\"messages\":[{\"role\":\"user\",\"content\":");agent_json_quote(&w,long_text);
    agent_json_raw(&w,"},{\"role\":\"assistant\",\"content\":");agent_json_quote(&w,long_text);
    agent_json_raw(&w,"}]}");assert(!w.error);
    assert(!agent_context_emit(&context,"turn","assistant",turn,NULL,0));
    assert(!agent_context_search(&context,"边",0,1,output,sizeof(output)));
    cJSON *root=agent_json_parse(output,strlen(output));assert(root);
    const char *snippet=agent_json_string(cJSON_GetObjectItemCaseSensitive(root,"hits")->child,"excerpt");
    assert(snippet && strlen(snippet)<=240 && agent_utf8_valid(snippet,strlen(snippet)) && strstr(snippet,"assistant:"));
    cJSON_Delete(root);
}

static void selection_cache_checks(void)
{
    memset(flash,255,sizeof(flash));agent_wal_t wal;
    assert(!agent_wal_format(&wal,&storage));boot();scenario=0;
    const char *turn="{\"messages\":[{\"role\":\"user\",\"content\":\"a\"},"
        "{\"role\":\"assistant\",\"content\":\"b\"}]}";
    for(unsigned i=0;i<AGENT_RECENT_MAX;++i)
        assert(!agent_context_emit(&context,"turn","assistant",turn,NULL,0));
    uint64_t before=context.wal.next_seq;
    flash_reads=0;assert(!agent_context_select(&context,before,NULL));
    assert(flash_reads && context.prompt_cached && context.prompt_count==AGENT_RECENT_MAX);
    written=0;assert(!agent_context_replay(&context,collect,NULL));strcpy(saved,wire);
    agent_context_release(&context);
    /* Appending a current tool result changes neither history membership nor
     * the previous wire bytes, and must not repeat selection's Flash scan. */
    assert(!agent_context_emit(&context,"tool_result","tool","{}",NULL,0));
    flash_reads=0;assert(!agent_context_select(&context,before,NULL));assert(!flash_reads);
    assert(agent_context_compact(&context)==AGENT_ERR_BUSY);
    written=0;assert(!agent_context_replay(&context,collect,NULL));
    assert(flash_reads && !strcmp(saved,wire));agent_context_release(&context);
    atomic_bool cancel=true;
    assert(agent_context_select(&context,before,&cancel)==AGENT_ERR_CANCELLED && !context.prompt_locked);
    atomic_store(&cancel,false);assert(!agent_context_select(&context,before,&cancel));
    atomic_store(&cancel,true);
    assert(agent_context_replay(&context,collect,NULL)==AGENT_ERR_CANCELLED && !context.prompt_cached);
    agent_context_release(&context);
    flash_reads=0;assert(!agent_context_select(&context,before,NULL));assert(flash_reads);
    agent_context_release(&context);
    /* Cache hits do not bypass CRC: corruption after selection is caught
     * before that record is written to the transport. */
    size_t bad=context.recent[context.prompt_start].offset+32;flash[bad]^=1;
    flash_reads=0;assert(!agent_context_select(&context,before,NULL));assert(!flash_reads);
    written=0;assert(agent_context_replay(&context,collect,NULL)==AGENT_ERR_CORRUPT);
    assert(!written && !context.prompt_cached);agent_context_release(&context);flash[bad]^=1;
    assert(!agent_context_select(&context,before,NULL));agent_context_release(&context);
    context.history_budget=1024;flash_reads=0;
    assert(!agent_context_select(&context,before,NULL));
    assert(flash_reads && context.prompt_bytes<=1024 && context.prompt_count<AGENT_RECENT_MAX);
    agent_context_release(&context);
    flash_reads=0;assert(!agent_context_select(&context,before-1,NULL));assert(flash_reads);
    agent_context_release(&context);
    /* Full index eviction must invalidate even though its count is unchanged. */
    assert(!agent_context_emit(&context,"turn","assistant",turn,NULL,0));
    assert(context.recent_count==AGENT_RECENT_MAX && !context.prompt_cached);
    flash_reads=0;assert(!agent_context_select(&context,before-1,NULL));assert(flash_reads);
    agent_context_release(&context);
    assert(!agent_context_compact(&context) && !context.prompt_cached);
    flash_reads=0;assert(!agent_context_select(&context,before-1,NULL));assert(flash_reads);
    agent_context_release(&context);
    /* Reopening the SAME context object also discards volatile metadata. */
    assert(!agent_context_open(&context,&storage) && !context.prompt_cached);
    flash_reads=0;assert(!agent_context_select(&context,before-1,NULL));assert(flash_reads);
    agent_context_release(&context);
    puts("Selection metadata reuse: unchanged bytes, eviction, budget/boundary, cancellation, CRC, compaction and reopen PASS");
}

int main(void)
{
    memset(flash,255,sizeof(flash)); agent_wal_t wal; assert(!agent_wal_format(&wal,&storage)); boot();
    context.history_budget=64u*1024u; /* Exercise the retained smaller budget explicitly. */
    char pad[1601],turn[4096]; memset(pad,'x',sizeof(pad)-1); pad[sizeof(pad)-1]=0;
    for(unsigned i=0;i<160;++i) {
        if(i%7==0) snprintf(turn,sizeof(turn),"{\"messages\":[{\"role\":\"user\",\"content\":\"ANCHOR_%03u 记忆 %s\"},{\"role\":\"assistant\",\"content\":null,\"tool_calls\":[{\"id\":\"hist-%u\",\"type\":\"function\",\"function\":{\"name\":\"device_status_get\",\"arguments\":\"{}\"}}]},{\"role\":\"tool\",\"tool_call_id\":\"hist-%u\",\"content\":\"{}\"},{\"role\":\"assistant\",\"content\":\"记住了\"}]}",i,pad,i,i);
        else snprintf(turn,sizeof(turn),"{\"messages\":[{\"role\":\"user\",\"content\":\"ANCHOR_%03u 记忆 %s\"},{\"role\":\"assistant\",\"content\":\"记住了\"}]}",i,pad);
        assert(!agent_context_emit(&context,"turn","assistant",turn,NULL,0));
    }
    assert(context.recent_count==128);
    assert(!agent_context_emit(&context,"turn","assistant",
        "{\"messages\":[{\"role\":\"user\",\"content\":\"引号\\\" 反斜杠\\\\ 换行\\n\"},"
        "{\"role\":\"assistant\",\"content\":\"UTF-8 与转义均保留\"}]}",NULL,0));
    size_t events=context.events;
    static const agent_transport_ops_t transport={http,NULL};
    static const agent_llm_ops_t llm={.build=agent_deepseek_request,.parse=agent_llm_parse,.compose=agent_deepseek_compose};
    static const agent_display_ops_t display={.status=display_status};
    static const agent_tool_ops_t tools={.light_set=light,.now_ms=now,.display=&display,
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
    assert(context.prompt_count>count64 && context.prompt_bytes>190*1024 && context.prompt_bytes<=200*1024);
    assert(context.events>events && context.pending==context.events);
    /* Identical durable history under two device uptimes: all bytes through
     * the historical prefix remain identical; only live state follows it.
     * Restore the test Flash image so this isolates uptime, not new history. */
    memcpy(prior_flash,flash,sizeof(flash));
    for(scenario=6;scenario<=7;++scenario) {
        if(scenario==7){memcpy(flash,prior_flash,sizeof(flash));boot();}
        uptime=scenario==6?1000:2000;requests=0;agent_core_init(&core);assert(!agent_begin_turn(&core));
        assert(!agent_engine_turn(&engine,"CURRENT_QUERY",false));agent_end_turn(&core);
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,no_snapshot,NULL));
    }
    /* The fast byte count and general plugin path must produce exactly the
     * same full HTTP body, including escapes and >190 KiB history. Restore
     * Flash between turns, so this compares the same durable input. */
    uptime=1000;memcpy(prior_flash,flash,sizeof(flash));
    agent_context_ops_t custom_context=agent_context_ops;custom_context.replay=custom_replay;
    agent_llm_ops_t custom_llm=llm;custom_llm.compose=custom_compose;
    for(scenario=8;scenario<=10;++scenario) {
        memcpy(flash,prior_flash,sizeof(flash));boot();flash_reads=0;requests=0;
        engine.context_ops=scenario==9?&custom_context:&agent_context_ops;
        engine.llm=scenario==10?&custom_llm:&llm;
        agent_core_init(&core);assert(!agent_begin_turn(&core));
        assert(!agent_engine_turn(&engine,"CURRENT_QUERY",false));agent_end_turn(&core);
        assert(requests==1 && context.prompt_bytes>190*1024 && !context.prompt_locked);
        if(scenario==8) optimized_reads=flash_reads;
        else assert(flash_reads>optimized_reads);
    }
    printf("Same request body: fast count %u Flash reads, general composer %u\n",optimized_reads,flash_reads);
    engine.context_ops=&agent_context_ops;engine.llm=&llm;scenario=0;
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
    context.now_ms=expired_clock;
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
    assert(!agent_context_prompt(&context,&engine.workspace->messages,"system") && strstr(engine.workspace->messages.data,"ANCHOR_000"));
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
    recall_answer_checks();
    selection_cache_checks();
    puts("64/128 KiB Flash prompts, paired recall excerpts, durable summary, exact framing, tool IDs, retry, cancellation, CRC, compaction lock and reboot PASS");
}
