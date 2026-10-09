#include "engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
    PLAIN, ONE_TOOL, FOUR_TOOLS, BACKPRESSURE, STALL, CANCEL_WAIT,
    BEGIN_FAIL, WRITE_FAIL, END_FAIL, LATE_TOOL, DISCONNECT, TRUNCATED,
    BAD_JSON, MISSING_DONE, INVALID_TOOL, UTF8_CHUNKS, WAL_FAIL,
    LONG_HISTORY, NO_SENT_HOOK, USB_STREAM, USB_NONSTREAM,
    PROGRESS_JOIN_FAIL, PROGRESS_JOIN_CANCEL, CASES
};
static unsigned char flash[2*1024*1024];
static agent_engine_storage_t engine_workspace;
/* Separate globals give ASan a red zone between the two actual turn buffers. */
static agent_engine_workspace_t split_work;
static _Alignas(8) char split_buffer[AGENT_ENGINE_BUFFER_SIZE];
static agent_engine_t engine={AGENT_ENGINE_STORAGE_INIT(engine_workspace)};
static agent_context_t context;
static agent_context_ops_t context_ops;
static agent_core_t core;
static char wire[AGENT_HTTP_REQUEST_MAX+1], spoken[AGENT_ANSWER_MAX+1], output[AGENT_ANSWER_MAX+1];
static size_t wire_used,spoken_used,output_used;
static unsigned scenario,requests,effects,begins,ends,writes,pauses,turns_saved;
static unsigned tool_joins;
static unsigned tool_result_appends;
static bool fail_tool_event;
enum { HINT_OFF,HINT_OK,HINT_WHITESPACE,HINT_OTHER,HINT_EMPTY,HINT_EMPTY_OBJECT,HINT_PENDING,HINT_REJECTED,HINT_UNKNOWN,
       HINT_INVALID,HINT_META_BAD,HINT_CANCEL,HINT_TOOL_ERROR,HINT_WAL_ERROR,HINT_LAST_OF_FOUR,HINT_CASES };
static unsigned batch_hint;
static uint64_t reserved;
static bool borrowed,body_sent,http_done,early;
static bool progress_borrowed,progress_http_fail,progress_cancel;
static bool cached_owned,cached_heard;
static unsigned progress_begins,progress_ends;
static agent_err_t progress_begin_error,progress_end_error;
static char input_alias[64];
static const char progress_phrase[]="嗯，想知道灯的名字呀，让我想想。";
static agent_err_t end_reason;
static const char *direct_text;
enum { DIRECT_OK, DIRECT_TRUNCATED, DIRECT_DISCONNECT, DIRECT_MISSING_DONE, DIRECT_CANCEL };
static unsigned direct_fault;
static unsigned preparations;
static agent_err_t preparation_error;
static uint8_t snapshot_rgb[3];
static unsigned snapshot_reads;
static agent_err_t snapshot_error;
static agent_err_t light_read(void *p,uint8_t rgb[3])
{
    (void)p;++snapshot_reads;
    memcpy(rgb,snapshot_rgb,3);
    return snapshot_error;
}
static agent_err_t prepare_answer(void *ctx)
{
    (void)ctx;
    assert(body_sent && !http_done && !engine.answer_final && !borrowed && !progress_borrowed);
    assert(!begins && !spoken_used && !effects && engine.answer_prepared);
    ++preparations;return preparation_error;
}

static agent_err_t rd(void *p,size_t o,void *d,size_t n)
{ (void)p;assert(o+n<=sizeof(flash));memcpy(d,flash+o,n);return AGENT_OK; }
static agent_err_t wr(void *p,size_t o,const void *d,size_t n)
{
    (void)p;assert(o+n<=sizeof(flash));const unsigned char *b=d;
    for(size_t i=0;i<n;++i){assert((flash[o+i]&b[i])==b[i]);flash[o+i]&=b[i];}
    return AGENT_OK;
}
static agent_err_t erase(void *p,size_t o,size_t n)
{ (void)p;assert(o+n<=sizeof(flash));memset(flash+o,255,n);return AGENT_OK; }
static agent_err_t reserve(void *p,uint64_t *first,uint64_t *last)
{ (void)p;*first=reserved+1;reserved+=128;*last=reserved;return AGENT_OK; }
static uint64_t now(void *p) { (void)p;return 1000+pauses*25; }
static void pause_ms(unsigned n)
{ assert(n==25);++pauses;if(scenario==CANCEL_WAIT)agent_cancel(&core); }
static agent_err_t light(void *p,const uint8_t rgb[3])
{
    (void)p;assert(tool_joins==effects+1 && rgb[0]==4 && rgb[1]==2 && rgb[2]==1);
    if(batch_hint==HINT_TOOL_ERROR)return AGENT_ERR_NETWORK;
    ++effects;return AGENT_OK;
}
static agent_err_t status(void *p,char *out,size_t capacity)
{
    (void)p;assert(tool_joins==effects+1);++effects;
    snprintf(out,capacity,"%s",batch_hint==HINT_EMPTY?"{\"hits\":[]}":
        batch_hint==HINT_EMPTY_OBJECT?"{}":batch_hint==HINT_PENDING?"{\"pending\":true}":batch_hint==HINT_UNKNOWN?"[]":
        "{\"executed\":false,\"error\":\"retry_needed\"}");
    return AGENT_OK;
}
static agent_err_t before_tools(void *p)
{
    assert(p==&tool_joins && !begins && !spoken_used && !borrowed && !progress_borrowed && !cached_owned);
    /* The complete batch is valid and no tool from it has executed yet. */
    assert(!agent_tools_validate(&engine.workspace->reply) && tool_joins==effects);
    ++tool_joins;
    if(scenario==PROGRESS_JOIN_FAIL)return AGENT_ERR_NETWORK;
    if(scenario==PROGRESS_JOIN_CANCEL || batch_hint==HINT_CANCEL)agent_cancel(&core);
    return AGENT_OK;
}
static agent_err_t display_status(void *p,char *out,size_t cap)
{ (void)p;snprintf(out,cap,"{\"mode\":\"off\",\"ready\":false}");return AGENT_OK; }

static void tail_check(void)
{
    if(!borrowed && !progress_borrowed)return;
    for(size_t i=AGENT_ENGINE_ANSWER_SCRATCH;i<AGENT_ENGINE_BUFFER_SIZE;++i)
        assert((unsigned char)engine.buffer[i]==0xa5);
}
static agent_err_t progress_begin(void *p)
{
    (void)p;assert(body_sent && !http_done && !borrowed && !progress_borrowed && !engine.answer_final);
    assert(engine.workspace->sse.capacity==AGENT_STREAM_MAX+1);
    progress_borrowed=true;++progress_begins;
    memset(engine.buffer+AGENT_ENGINE_ANSWER_SCRATCH,0xa5,AGENT_ENGINE_BUFFER_SIZE-AGENT_ENGINE_ANSWER_SCRATCH);
    /* Production reuses the original ASR input for its TTS text queue. */
    strcpy(input_alias,"ACK_QUEUE_REUSED");
    if(progress_cancel)agent_cancel(&core);
    return progress_begin_error;
}
static agent_err_t progress_end(void *p,agent_err_t error)
{
    (void)p;assert(progress_borrowed && !borrowed && !engine.answer_final);tail_check();
    progress_borrowed=false;++progress_ends;
    if(!error && !progress_end_error) {engine.progress_text=progress_phrase;engine.progress_language="yue";}
    return error?error:progress_end_error;
}
/* A local PCM job already owns the speaker before the first planning send.
 * Register that owner without borrowing scratch or replacing the ASR input. */
static agent_err_t cached_begin(void *p)
{
    (void)p;assert(cached_owned && body_sent && !engine.answer_final);
    assert(!borrowed && !progress_borrowed && !strcmp(input_alias,"USER_REQUEST"));
    ++progress_begins;
    if(progress_cancel)agent_cancel(&core);
    return AGENT_OK;
}
static agent_err_t cached_end(void *p,agent_err_t producer_error)
{
    (void)p;(void)producer_error;
    if(!cached_owned)return AGENT_OK;
    assert(!borrowed && !progress_borrowed);
    cached_owned=false;++progress_ends;
    if(!progress_end_error) {
        cached_heard=true;engine.progress_text=progress_phrase;engine.progress_language="yue";
    }
    /* The engine retains any producer error after the local owner joins. */
    return progress_end_error;
}
static agent_err_t begin(void *p)
{
    (void)p;assert(body_sent && (!http_done || direct_text) && !borrowed && !progress_borrowed && !cached_owned && engine.answer_final);
    assert(engine.workspace->sse.capacity==AGENT_STREAM_MAX+1);
    borrowed=true;++begins;
    memset(engine.buffer+AGENT_ENGINE_ANSWER_SCRATCH,0xa5,AGENT_ENGINE_BUFFER_SIZE-AGENT_ENGINE_ANSWER_SCRATCH);
    return scenario==BEGIN_FAIL?AGENT_ERR_MEMORY:AGENT_OK;
}
static agent_err_t speak(void *p,const char *text,size_t n)
{
    (void)p;assert(borrowed && n && n<=AGENT_ENGINE_ANSWER_CHUNK);
    assert(agent_utf8_valid(text,n));tail_check();++writes;
    assert(effects==(engine.clarify_only?0u:scenario==ONE_TOOL?1u:scenario==FOUR_TOOLS?4u:0u));
    if((scenario==BACKPRESSURE && writes<=2) || scenario==STALL || scenario==CANCEL_WAIT)return AGENT_ERR_BUSY;
    if(scenario==WRITE_FAIL)return AGENT_ERR_SERVER;
    assert(spoken_used+n<sizeof(spoken));memcpy(spoken+spoken_used,text,n);spoken_used+=n;spoken[spoken_used]=0;
    if(!http_done)early=true;
    return AGENT_OK;
}
static agent_err_t end(void *p,agent_err_t error)
{
    (void)p;assert(borrowed);tail_check();borrowed=false;++ends;end_reason=error;
    return scenario==END_FAIL?AGENT_ERR_NETWORK:AGENT_OK;
}
static agent_err_t append(agent_context_t *c,const char *type,const char *actor,const char *content,char *id,size_t cap)
{
    assert(!borrowed && !progress_borrowed); /* WAL never shares a live speech tail. */
    /* Local immutable PCM borrows no scratch: the initial user event can be
     * written while it plays. Tools and completed turns follow its join. */
    if(!strcmp(type,"tool_result") || !strcmp(type,"turn"))assert(!cached_owned);
    assert(!strstr(content,"ACK_QUEUE_REUSED"));
    if(!strcmp(type,"tool_result"))++tool_result_appends;
    if(batch_hint==HINT_WAL_ERROR && !strcmp(type,"tool_result"))return AGENT_ERR_STORAGE;
    if(!strcmp(type,"turn")) {
        assert(scenario==USB_STREAM || scenario==USB_NONSTREAM || ends==1);
        assert(!strstr(content,"READY") && !strstr(content,"Planning is complete"));
        assert(!strstr(content,"Current-turn display snapshot"));
        assert(!strstr(content,"Current-turn light snapshot"));
        assert(!strstr(content,progress_phrase));
        if(scenario==WAL_FAIL)return AGENT_ERR_STORAGE;
        ++turns_saved;
    }
    return agent_context_emit(c,type,actor,content,id,cap);
}
static agent_err_t output_event(void *p,const agent_event_t *event)
{
    (void)p;
    if(event->type==AGENT_EVENT_TOOL_END && fail_tool_event && effects)return AGENT_ERR_SERVER;
    if(event->type==AGENT_EVENT_TEXT) {
        /* A slow terminal may block this callback. The entire corresponding
         * delta must already be published to TTS before we enter it. */
        if(engine.answer_final) {
            assert(spoken_used>=output_used+event->length);
            assert(!memcmp(spoken+output_used,event->data,event->length));
        }
        assert(output_used+event->length<sizeof(output));
        memcpy(output+output_used,event->data,event->length);output_used+=event->length;output[output_used]=0;
    }
    return AGENT_OK;
}
static agent_err_t collect(void *p,const char *bytes,size_t n)
{
    (void)p;assert(!borrowed && !progress_borrowed && wire_used+n<sizeof(wire));
    memcpy(wire+wire_used,bytes,n);wire_used+=n;wire[wire_used]=0;return AGENT_OK;
}
static agent_err_t fragment(agent_http_feed_fn feed,void *ctx,const char *data,size_t n)
{
    for(size_t i=0;i<n;) {
        size_t m=1+(i*17)%31;if(m>n-i)m=n-i;
        agent_err_t error=feed(ctx,data+i,m);tail_check();if(error)return error;i+=m;
    }
    return AGENT_OK;
}
static agent_err_t text_delta(agent_http_feed_fn feed,void *ctx,const char *text)
{
    char frame[4096];agent_json_writer_t w;agent_json_writer_init(&w,frame,sizeof(frame));
    agent_json_raw(&w,"data: {\"choices\":[{\"delta\":{\"content\":");agent_json_quote(&w,text);
    agent_json_raw(&w,"}}]}\r\n\r\n");assert(!w.error);return fragment(feed,ctx,frame,w.used);
}
static agent_err_t finish(agent_http_feed_fn feed,void *ctx,const char *reason,bool done)
{
    char frame[192];int n=snprintf(frame,sizeof(frame),"data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"%s\"}]}\n\n%s",reason,done?"data: [DONE]\n\n":"");
    assert(n>0 && (size_t)n<sizeof(frame));return fragment(feed,ctx,frame,(size_t)n);
}
static agent_err_t tool(agent_http_feed_fn feed,void *ctx,bool invalid)
{
    if(batch_hint==HINT_EMPTY || batch_hint==HINT_EMPTY_OBJECT || batch_hint==HINT_PENDING ||
       batch_hint==HINT_REJECTED || batch_hint==HINT_UNKNOWN) {
        static const char read[]="data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"status-1\",\"type\":\"function\",\"function\":{\"name\":\"device_status_get\",\"arguments\":\"{\\\"final_batch\\\":true}\"}}]}}]}\n\n";
        return fragment(feed,ctx,read,sizeof(read)-1);
    }
    bool hinted=batch_hint && batch_hint!=HINT_OTHER &&
        (batch_hint!=HINT_LAST_OF_FOUR || requests==4);
    const char *meta=!hinted?"":
        batch_hint==HINT_META_BAD?",\\\"final_batch\\\":\\\"true\\\"":
        batch_hint==HINT_WHITESPACE?", \\\"final_batch\\\" : true ":",\\\"final_batch\\\":true";
    char frame[1024];int n=snprintf(frame,sizeof(frame),
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"id-%u\",\"type\":\"function\",\"function\":{\"name\":\"device_light_set_rgb\",\"arguments\":\"{\\\"r\\\":%u,\\\"g\\\":2,\\\"b\\\":1%s}\"}}]}}]}\n\n",requests,invalid?999u:4u,meta);
    assert(n>0 && (size_t)n<sizeof(frame));return fragment(feed,ctx,frame,(size_t)n);
}
static void check_wire(bool final)
{
    const char *parse_end=NULL;
    cJSON *root=cJSON_ParseWithLengthOpts(wire,wire_used,&parse_end,false);
    if(!root)fprintf(stderr,"wire parse failed scenario=%u final=%u near %.100s\n",scenario,final,parse_end?parse_end:"unknown");
    assert(root && parse_end==wire+wire_used);
    const cJSON *messages=cJSON_GetObjectItemCaseSensitive(root,"messages");assert(cJSON_IsArray(messages));
    assert(strstr(wire,"ANCHOR_HISTORY") && strstr(wire,"USER_REQUEST"));
    const char *snapshot=strstr(wire,"Current-turn display snapshot");assert(snapshot);
    assert(snapshot>strstr(wire,"ANCHOR_HISTORY") && snapshot<strstr(wire,"USER_REQUEST"));
    assert(!strstr(agent_json_string(messages->child,"content"),"Current-turn display snapshot"));
    unsigned lights=0;
    for(const cJSON *m=messages->child;m;m=m->next) {
        const char *text=agent_json_string(m,"content");
        if(text && strstr(text,"Current-turn light snapshot")) {
            ++lights;
            assert(!strcmp(agent_json_string(m,"role"),"system"));
            assert(strstr(text,"uptime 1000 ms"));
            if(snapshot_error) {
                assert(strstr(text,"unavailable: network") && !strstr(text,"\"r\":"));
            } else {
                char rgb[64];snprintf(rgb,sizeof(rgb),"{\"r\":%u,\"g\":%u,\"b\":%u}",
                    snapshot_rgb[0],snapshot_rgb[1],snapshot_rgb[2]);
                assert(strstr(text,rgb));
            }
            assert(m->next && !strcmp(agent_json_string(m->next,"role"),"user"));
            assert(!strncmp(agent_json_string(m->next,"content"),"USER_REQUEST",12));
        }
    }
    assert(lights==(engine.tools->light_get?1u:0u));
    assert(snapshot_reads==(engine.tools->light_get?1u:0u));
    if(scenario==LONG_HISTORY)assert(context.history_budget==200u*1024u && context.prompt_bytes>190u*1024u && wire_used>190u*1024u);
    if(final) {
        assert(!!strstr(wire,progress_phrase)==!!engine.progress_text);
        assert(!!strstr(wire,"Continue this spoken answer in Cantonese.")==!!engine.progress_language);
        assert(!cJSON_GetObjectItemCaseSensitive(root,"tools"));
        assert(!strcmp(agent_json_string(root,"tool_choice"),"none"));
        assert(strstr(wire,engine.clarify_only?"ASR lacks details":
            core.tool_rounds>=AGENT_ROUNDS_MAX?"Tool-call budget reached":"Planning is complete"));
        if(core.tool_rounds>=AGENT_ROUNDS_MAX)assert(strstr(wire,"completion is unconfirmed"));
        assert(!strstr(wire,"This is the planning stage"));
        unsigned pairs=0;
        for(const cJSON *m=messages->child;m;m=m->next)
            if(!strcmp(agent_json_string(m,"role"),"tool")) {assert(agent_json_string(m,"tool_call_id"));++pairs;}
        assert(pairs==effects);
    } else {
        assert(!strstr(wire,progress_phrase));
        assert(cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root,"tools")));
        assert(!cJSON_GetObjectItemCaseSensitive(root,"tool_choice"));
        if(engine.answer_stream)assert(strstr(wire,"This is the planning stage"));
    }
    cJSON_Delete(root);
}
static agent_err_t http(void *p,const agent_http_request_t *r,agent_http_feed_fn feed,void *ctx)
{
    (void)p;++requests;wire_used=0;body_sent=false;http_done=false;
    assert(!agent_http_write_body(r,collect,NULL) && wire_used==r->length);
    body_sent=true;check_wire(engine.answer_final);
    if(r->on_sent && scenario!=NO_SENT_HOOK) {agent_err_t error=r->on_sent(r->sent_ctx);if(error)return error;}
    if(scenario==USB_NONSTREAM) {
        const char reply[]="{\"choices\":[{\"message\":{\"content\":\"Normal USB reply\"},\"finish_reason\":\"stop\"}]}";
        return feed(ctx,reply,sizeof(reply)-1);
    }
    if(scenario==USB_STREAM) {
        agent_err_t error=text_delta(feed,ctx,"Normal USB reply");
        return error?error:finish(feed,ctx,"stop",true);
    }
    if(!engine.answer_final) {
        assert(!begins);
        if(progress_http_fail)return AGENT_ERR_NETWORK;
        if(scenario==FOUR_TOOLS || requests<=(scenario==ONE_TOOL || scenario==PROGRESS_JOIN_FAIL || scenario==PROGRESS_JOIN_CANCEL?1u:scenario==INVALID_TOOL?3u:0u)) {
            /* Deliberately put a misleading preamble before a later tool call.
             * It must not be voiced and no action occurs before final JSON. */
            agent_err_t error=text_delta(feed,ctx,direct_text?direct_text:"Temporary action preamble.");
            if(!error)error=tool(feed,ctx,scenario==INVALID_TOOL);
            if(!error)error=finish(feed,ctx,"tool_calls",true);
            assert(!begins && !spoken_used);return error;
        }
        agent_err_t error=text_delta(feed,ctx,direct_text?(effects?"ANSWER:已根据工具结果核对。":direct_text):"READY");
        if(direct_text && direct_fault==DIRECT_DISCONNECT)return AGENT_ERR_NETWORK;
        if(!error)error=finish(feed,ctx,direct_text && direct_fault==DIRECT_TRUNCATED?"length":"stop",
                              !direct_text || direct_fault!=DIRECT_MISSING_DONE);
        if(direct_text) {
            http_done=true;
            if(direct_fault==DIRECT_CANCEL)agent_cancel(&core);
        }
        assert(!begins && !spoken_used);return error;
    }
    if(scenario==LATE_TOOL)return tool(feed,ctx,false);
    char large[1201];
    for(size_t i=0;i<sizeof(large)-1;i+=6)memcpy(large+i,"你好",6);
    large[sizeof(large)-1]=0;
    agent_err_t error=text_delta(feed,ctx,scenario==UTF8_CHUNKS?large:"First sentence. ");
    if(error)return error;
    assert(begins==1 && early && !ends);
    if(scenario==DISCONNECT)return AGENT_ERR_NETWORK;
    if(scenario==BAD_JSON)return fragment(feed,ctx,"data: {broken}\n\n",16);
    error=text_delta(feed,ctx,"Last sentence.");
    if(!error)error=finish(feed,ctx,scenario==TRUNCATED?"length":"stop",scenario!=MISSING_DONE);
    http_done=true;return error;
}
static agent_err_t inspect(void *p,const agent_record_t *r,const char *data)
{
    (void)p;if(r->kind!=AGENT_WAL_EVENT)return AGENT_OK;
    assert(!strstr(data,"READY") && !strstr(data,"Planning is complete"));
    assert(!strstr(data,"Current-turn display snapshot"));
    assert(!strstr(data,"Current-turn light snapshot"));
    assert(!strstr(data,progress_phrase));
    assert(!strstr(data,"final_batch"));
    return AGENT_OK;
}
static void reset(void)
{
    memset(&engine,0,sizeof(engine));
    assert(!agent_engine_bind_parts(&engine,&split_work,sizeof(split_work),split_buffer,sizeof(split_buffer)));
    memset(&split_work,0,sizeof(split_work));memset(split_buffer,0,sizeof(split_buffer));
    memset(&context,0,sizeof(context));memset(flash,255,sizeof(flash));
    requests=effects=begins=ends=writes=pauses=turns_saved=tool_joins=0;wire_used=spoken_used=output_used=0;
    tool_result_appends=0;fail_tool_event=false;
    batch_hint=HINT_OFF;
    borrowed=body_sent=http_done=early=false;spoken[0]=output[0]=0;end_reason=AGENT_OK;
    progress_borrowed=progress_http_fail=progress_cancel=false;progress_begins=progress_ends=0;
    cached_owned=cached_heard=false;
    progress_begin_error=progress_end_error=AGENT_OK;
    direct_text=NULL;direct_fault=DIRECT_OK;
    preparations=0;preparation_error=AGENT_OK;
    memset(snapshot_rgb,0,sizeof(snapshot_rgb));snapshot_reads=0;snapshot_error=AGENT_OK;
    strcpy(input_alias,"USER_REQUEST");
    static const agent_flash_ops_t storage={rd,wr,erase,NULL,sizeof(flash),4096};
    static const agent_display_ops_t display={.status=display_status};
    static const agent_tool_ops_t tools={.light_set=light,.light_get=light_read,.status=status,.now_ms=now,.display=&display};
    static const agent_llm_ops_t llm={.parse=agent_llm_parse,.compose=agent_deepseek_compose,.compose_final=agent_deepseek_compose_final};
    static const agent_transport_ops_t transport={http,NULL};
    agent_wal_t wal;assert(!agent_wal_format(&wal,&storage));
    context.device="stream-test";context.user="user";context.session="session";context.reserve=reserve;
    context.scratch=engine.buffer;context.capacity=AGENT_ENGINE_BUFFER_SIZE;assert(!agent_context_open(&context,&storage));
    /* History wider than the speech scratch boundary must be replayed before
     * speech borrows that tail; no reduction of the history budget is allowed. */
    char history[10000];memset(history,'x',sizeof(history));
    const char prefix[]="{\"messages\":[{\"role\":\"user\",\"content\":\"ANCHOR_HISTORY ";
    const char suffix[]="\"},{\"role\":\"assistant\",\"content\":\"prior\"}]}";
    memcpy(history,prefix,sizeof(prefix)-1);memcpy(history+9500,suffix,sizeof(suffix));
    agent_err_t saved=agent_context_emit(&context,"turn","assistant",history,NULL,0);
    if(saved)fprintf(stderr,"history error: %s\n",agent_err_name(saved));
    assert(!saved);
    if(scenario==LONG_HISTORY)
        for(unsigned i=1;i<24;++i)assert(!agent_context_emit(&context,"turn","assistant",history,NULL,0));
    context_ops=agent_context_ops;context_ops.append=append;
    engine.core=&core;engine.context=&context;engine.context_ops=&context_ops;engine.tools=&tools;
    engine.llm=&llm;engine.transport=&transport;engine.pause_ms=pause_ms;engine.emit=output_event;
    engine.voice_mode=scenario!=USB_STREAM && scenario!=USB_NONSTREAM;
    engine.answer_begin=begin;engine.answer_write=speak;engine.answer_end=end;
    engine.before_tools=before_tools;engine.before_tools_ctx=&tool_joins;
    agent_core_init(&core);assert(!agent_begin_turn(&core));
}
static void progress_checks(void)
{
    for(unsigned failure=0;failure<2;++failure) {
        scenario=PLAIN;reset();engine.heard_ack=progress_phrase;engine.heard_ack_language="yue";
        engine.progress_begin=progress_begin;engine.progress_end=progress_end;
        progress_http_fail=failure!=0;
        assert(agent_engine_turn(&engine,input_alias,true)==(failure?AGENT_ERR_NETWORK:AGENT_OK));
        assert(!progress_begins && !progress_ends && !progress_borrowed && !borrowed);
        assert(requests==(failure?1u:2u) && engine.progress_attempted && engine.progress_text==progress_phrase);
        assert(!strstr(spoken,progress_phrase));
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect,NULL));
        agent_end_turn(&core);
    }
    const unsigned cases[]={PLAIN,ONE_TOOL,FOUR_TOOLS,INVALID_TOOL,LONG_HISTORY,NO_SENT_HOOK,USB_STREAM,USB_NONSTREAM};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        scenario=cases[i];reset();engine.progress_begin=progress_begin;engine.progress_end=progress_end;
        agent_err_t error=agent_engine_turn(&engine,input_alias,scenario!=USB_NONSTREAM);
        assert(error==(scenario==INVALID_TOOL?AGENT_ERR_ARGUMENT:AGENT_OK));
        bool enabled=scenario!=NO_SENT_HOOK && scenario!=USB_STREAM && scenario!=USB_NONSTREAM;
        assert(progress_begins==enabled && progress_ends==enabled && !progress_borrowed && !borrowed);
        assert(!strstr(spoken,progress_phrase) && !strstr(output,progress_phrase) && !context.prompt_locked);
        if(enabled && !error)assert(engine.progress_text==progress_phrase);
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect,NULL));
        agent_end_turn(&core);
    }
    for(unsigned failure=0;failure<4;++failure) {
        scenario=PLAIN;reset();engine.progress_begin=progress_begin;engine.progress_end=progress_end;
        if(failure==0)progress_begin_error=AGENT_ERR_MEMORY;
        if(failure==1)progress_end_error=AGENT_ERR_NETWORK;
        if(failure==2)progress_http_fail=true;
        if(failure==3)progress_cancel=true;
        agent_err_t expected=failure==0?AGENT_ERR_MEMORY:failure==3?AGENT_ERR_CANCELLED:AGENT_ERR_NETWORK;
        assert(agent_engine_turn(&engine,input_alias,true)==expected);
        /* No retry, new request, tool or WAL may overlap the acknowledgement;
         * even a partially failed begin must be joined exactly once. */
        assert(requests==1 && progress_begins==1 && progress_ends==1 && !progress_borrowed && !borrowed);
        assert(!begins && !ends && !effects && !turns_saved && !context.prompt_locked && !engine.progress_text);
        agent_end_turn(&core);
    }
}
static void cached_owner_checks(void)
{
    const unsigned cases[]={PLAIN,ONE_TOOL,FOUR_TOOLS};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        scenario=cases[i];reset();cached_owned=true;
        engine.progress_begin=cached_begin;engine.progress_end=cached_end;
        assert(!agent_engine_turn(&engine,input_alias,true));
        assert(!cached_owned && cached_heard && progress_begins==1 && progress_ends==1);
        assert(requests==(i==0?2u:i==1?3u:5u) && effects==(i==0?0u:i==1?1u:4u));
        assert(engine.progress_text==progress_phrase && !strcmp(input_alias,"USER_REQUEST"));
        assert(!progress_borrowed && !borrowed && !context.prompt_locked && turns_saved==1);
        assert(!strstr(spoken,progress_phrase) && !strstr(output,progress_phrase));
        assert(!cached_end(NULL,AGENT_OK) && progress_ends==1); /* Caller cleanup is idempotent. */
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect,NULL));
        agent_end_turn(&core);
    }
    for(unsigned failure=0;failure<4;++failure) {
        scenario=PLAIN;reset();cached_owned=true;
        engine.progress_begin=cached_begin;engine.progress_end=cached_end;
        if(failure==0)progress_http_fail=true;
        if(failure==1)progress_end_error=AGENT_ERR_TIMEOUT;
        if(failure==2)progress_cancel=true;
        if(failure==3)agent_cancel(&core); /* Error before the on_sent callback. */
        const agent_err_t expected=failure==0?AGENT_ERR_NETWORK:
            failure==1?AGENT_ERR_TIMEOUT:AGENT_ERR_CANCELLED;
        assert(agent_engine_turn(&engine,input_alias,true)==expected);
        if(cached_owned)assert(!cached_end(NULL,expected)); /* Runtime's mandatory terminal join. */
        assert(!cached_owned && progress_ends==1 && progress_begins==(failure==3?0u:1u));
        assert(requests==(failure==3?0u:1u) && !effects && !begins && !ends && !turns_saved);
        assert(cached_heard==(failure!=1) && !!engine.progress_text==cached_heard);
        assert(!progress_borrowed && !borrowed && !context.prompt_locked && !strcmp(input_alias,"USER_REQUEST"));
        agent_end_turn(&core);
    }
}
static void clarification_checks(void)
{
    const unsigned cases[]={PLAIN,ONE_TOOL,LATE_TOOL,WRITE_FAIL};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        scenario=cases[i];reset();engine.clarify_only=true;
        agent_err_t error=agent_engine_turn(&engine,"USER_REQUEST 不对，不。",true);
        assert(error==(scenario==LATE_TOOL?AGENT_ERR_PROTOCOL:scenario==WRITE_FAIL?AGENT_ERR_SERVER:AGENT_OK));
        assert(requests==1 && !effects && !tool_joins && !progress_begins && !progress_ends);
        assert(!context.prompt_locked && !borrowed && turns_saved==!error);
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect,NULL));
        agent_end_turn(&core);
    }
    for(unsigned mode=0;mode<3;++mode) {
        scenario=PLAIN;reset();engine.clarify_only=true;
        if(mode==0)engine.gateway=true;
        if(mode==1)engine.voice_mode=false;
        assert(agent_engine_turn(&engine,"USER_REQUEST",mode!=2)==AGENT_ERR_CONFIG);
        assert(!requests && !effects && !begins && !turns_saved);agent_end_turn(&core);
    }
}
static void final_batch_checks(void)
{
    for(unsigned hint=HINT_OK;hint<HINT_CASES;++hint) {
        scenario=hint==HINT_INVALID?INVALID_TOOL:hint==HINT_LAST_OF_FOUR?FOUR_TOOLS:ONE_TOOL;reset();batch_hint=hint;
        agent_err_t expected=hint==HINT_INVALID || hint==HINT_META_BAD?AGENT_ERR_ARGUMENT:
            hint==HINT_CANCEL?AGENT_ERR_CANCELLED:hint==HINT_TOOL_ERROR?AGENT_ERR_NETWORK:
            hint==HINT_WAL_ERROR?AGENT_ERR_STORAGE:AGENT_OK;
        agent_err_t error=agent_engine_turn(&engine,"USER_REQUEST",true);
        assert(error==expected && !borrowed && !context.prompt_locked);
        bool shortcut=hint==HINT_OK || hint==HINT_WHITESPACE;
        assert(requests==(hint==HINT_LAST_OF_FOUR?5u:shortcut?2u:hint==HINT_META_BAD || hint==HINT_CANCEL || hint==HINT_TOOL_ERROR || hint==HINT_WAL_ERROR?1u:3u));
        if(!error)assert(effects==(hint==HINT_LAST_OF_FOUR?4u:1u) && begins==1 && ends==1 && early && turns_saved==1);
        else assert(!begins && !ends && !spoken_used && !turns_saved);
        assert(!strstr(spoken,"READY") && !strstr(output,"READY"));
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect,NULL));
        agent_end_turn(&core);
    }
}
static void preparation_checks(void)
{
    const unsigned cases[]={PLAIN,ONE_TOOL,FOUR_TOOLS,LONG_HISTORY,INVALID_TOOL,NO_SENT_HOOK,USB_STREAM,USB_NONSTREAM};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        scenario=cases[i];reset();engine.answer_prepare=prepare_answer;
        agent_err_t error=agent_engine_turn(&engine,"USER_REQUEST",scenario!=USB_NONSTREAM);
        assert(error==(scenario==INVALID_TOOL?AGENT_ERR_ARGUMENT:AGENT_OK));
        assert(preparations==(scenario!=NO_SENT_HOOK && scenario!=USB_STREAM && scenario!=USB_NONSTREAM));
        assert(!borrowed && !progress_borrowed && !context.prompt_locked);
        agent_end_turn(&core);
    }
    const agent_err_t failures[]={AGENT_ERR_MEMORY,AGENT_ERR_NETWORK,AGENT_ERR_CANCELLED};
    for(unsigned i=0;i<sizeof(failures)/sizeof(*failures);++i) {
        scenario=ONE_TOOL;reset();engine.answer_prepare=prepare_answer;preparation_error=failures[i];
        assert(agent_engine_turn(&engine,"USER_REQUEST",true)==failures[i]);
        assert(preparations==1 && requests==1 && !effects && !begins && !turns_saved && !context.prompt_locked);
        agent_end_turn(&core);
    }
    scenario=PLAIN;reset();engine.answer_prepare=prepare_answer;engine.clarify_only=true;
    assert(!agent_engine_turn(&engine,"USER_REQUEST 不对，不。",true));
    assert(!preparations && requests==1);agent_end_turn(&core);
    scenario=PLAIN;reset();engine.answer_prepare=prepare_answer;direct_text="ANSWER:完整回答。";
    assert(!agent_engine_turn(&engine,"USER_REQUEST",true));
    assert(preparations==1 && requests==1 && begins==1 && ends==1);agent_end_turn(&core);
    scenario=PLAIN;reset();engine.answer_prepare=prepare_answer;
    engine.progress_begin=progress_begin;engine.progress_end=progress_end;
    assert(!agent_engine_turn(&engine,input_alias,true));
    assert(preparations==1 && progress_begins==1 && progress_ends==1 && begins==1 && ends==1);
    agent_end_turn(&core);
    puts("Voice connection preparation: after body sent, once across tool rounds, no speech/workspace borrowing, fail without replay, USB/clarify unchanged OK");
}
static void direct_answer_checks(void)
{
    const char *texts[]={"ANSWER:我记得，这盏灯叫小星星。", " \nANSWER:\n 你好，小言。 \r\n",
                         "ANSWER:","ANSWER: READY ","READY","未标记的规划说明"};
    for(unsigned i=0;i<sizeof(texts)/sizeof(*texts);++i) {
        scenario=PLAIN;reset();direct_text=texts[i];
        assert(!agent_engine_turn(&engine,"USER_REQUEST",true));
        assert(requests==(i<2?1u:2u) && begins==1 && ends==1 && !effects && turns_saved==1);
        assert(!borrowed && !context.prompt_locked && !strstr(spoken,"ANSWER:") && !strstr(spoken,"READY"));
        assert(!strcmp(spoken,output) && !strcmp(spoken,engine.workspace->reply.text));
        if(i<2)assert(!early && !strchr(spoken,'\n'));
        agent_end_turn(&core);
    }
    for(unsigned failure=DIRECT_TRUNCATED;failure<=DIRECT_CANCEL;++failure) {
        scenario=PLAIN;reset();direct_text="ANSWER:不可播放的未完成回复。";direct_fault=failure;
        agent_err_t expected=failure==DIRECT_TRUNCATED?AGENT_ERR_LIMIT:failure==DIRECT_DISCONNECT?
            AGENT_ERR_NETWORK:failure==DIRECT_CANCEL?AGENT_ERR_CANCELLED:AGENT_ERR_PROTOCOL;
        assert(agent_engine_turn(&engine,"USER_REQUEST",true)==expected);
        assert(requests==1 && !begins && !ends && !spoken_used && !output_used && !turns_saved);
        assert(!borrowed && !context.prompt_locked);agent_end_turn(&core);
    }
    const unsigned failures[]={BEGIN_FAIL,WRITE_FAIL,END_FAIL,WAL_FAIL,BACKPRESSURE,STALL,CANCEL_WAIT};
    for(unsigned i=0;i<sizeof(failures)/sizeof(*failures);++i) {
        scenario=failures[i];reset();direct_text="ANSWER:完整回答。";
        agent_err_t expected=scenario==BEGIN_FAIL?AGENT_ERR_MEMORY:scenario==WRITE_FAIL?AGENT_ERR_SERVER:
            scenario==END_FAIL?AGENT_ERR_NETWORK:scenario==WAL_FAIL?AGENT_ERR_STORAGE:
            scenario==STALL?AGENT_ERR_TIMEOUT:scenario==CANCEL_WAIT?AGENT_ERR_CANCELLED:AGENT_OK;
        assert(agent_engine_turn(&engine,"USER_REQUEST",true)==expected);
        assert(requests==1 && begins==1 && ends==1 && !borrowed && !context.prompt_locked && turns_saved==!expected);
        assert(!effects && !tool_joins);agent_end_turn(&core);
    }
    /* A late tool suppresses the labelled preamble. The next complete answer
     * is based on a validated/executed/persisted tool result, with no third
     * generation or repetition of the effect. */
    scenario=ONE_TOOL;reset();direct_text="ANSWER:这句话之后还有工具调用。";
    assert(!agent_engine_turn(&engine,"USER_REQUEST",true));
    assert(requests==2 && effects==1 && tool_joins==1 && begins==1 && ends==1 && !early);
    assert(tool_result_appends==1 && !strcmp(spoken,"已根据工具结果核对。"));
    assert(!strstr(spoken,"这句话") && !strstr(output,"这句话"));agent_end_turn(&core);
    scenario=FOUR_TOOLS;reset();direct_text="ANSWER:尚未执行的草稿。";
    assert(!agent_engine_turn(&engine,"USER_REQUEST",true));
    assert(requests==5 && effects==4 && core.tool_rounds==4 && tool_result_appends==4);
    assert(begins==1 && ends==1 && early && turns_saved==1);agent_end_turn(&core);
    scenario=ONE_TOOL;reset();direct_text="ANSWER:不能重放的动作。";direct_fault=DIRECT_DISCONNECT;
    assert(agent_engine_turn(&engine,"USER_REQUEST",true)==AGENT_ERR_NETWORK);
    assert(requests==2 && effects==1 && tool_result_appends==1 && !begins && !ends && !turns_saved);
    assert(!context.prompt_locked && !borrowed);agent_end_turn(&core);
    scenario=ONE_TOOL;reset();direct_text="ANSWER:执行失败不能播报。";batch_hint=HINT_TOOL_ERROR;
    assert(agent_engine_turn(&engine,"USER_REQUEST",true)==AGENT_ERR_NETWORK);
    assert(requests==1 && !effects && !begins && !ends && !turns_saved);agent_end_turn(&core);
    /* Progress releases the shared scratch before buffered speech, and only
     * the real full input plus the answer enter the durable history. */
    scenario=PLAIN;reset();direct_text="ANSWER:完整回答。";
    engine.progress_begin=progress_begin;engine.progress_end=progress_end;
    assert(!agent_engine_turn(&engine,input_alias,true));
    assert(requests==1 && progress_begins==1 && progress_ends==1 && begins==1 && ends==1);
    assert(!progress_borrowed && !borrowed && !context.prompt_locked && turns_saved==1);
    assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect,NULL));
    agent_end_turn(&core);
}
static void light_snapshot_checks(void)
{
    static const uint8_t states[][3]={{0,0,0},{0,255,0},{0,0,255},{255,255,255}};
    for(unsigned i=0;i<5;++i) {
        scenario=PLAIN;reset();
        if(i<4)memcpy(snapshot_rgb,states[i],3);
        if(i==3)snapshot_error=AGENT_ERR_NETWORK; /* A failed read must not leak its RGB. */
        agent_tool_ops_t tools=*engine.tools;
        if(i==4)tools.light_get=NULL;
        engine.tools=&tools;
        assert(!agent_engine_turn(&engine,"USER_REQUEST",true));
        assert(!effects && snapshot_reads==(i==4?0u:1u));
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect,NULL));
        agent_end_turn(&core);
    }
}
int main(void)
{
    static const agent_err_t errors[CASES]={
        AGENT_OK,AGENT_OK,AGENT_OK,AGENT_OK,AGENT_ERR_TIMEOUT,AGENT_ERR_CANCELLED,
        AGENT_ERR_MEMORY,AGENT_ERR_SERVER,AGENT_ERR_NETWORK,AGENT_ERR_PROTOCOL,AGENT_ERR_NETWORK,AGENT_ERR_LIMIT,
        AGENT_ERR_JSON,AGENT_ERR_PROTOCOL,AGENT_ERR_ARGUMENT,AGENT_OK,AGENT_ERR_STORAGE,AGENT_OK,AGENT_OK,AGENT_OK,AGENT_OK,
        AGENT_ERR_NETWORK,AGENT_ERR_CANCELLED
    };
    for(scenario=0;scenario<CASES;++scenario) {
        reset();agent_err_t error=agent_engine_turn(&engine,"USER_REQUEST",scenario!=USB_NONSTREAM);
        agent_end_turn(&core);
        if(error!=errors[scenario])fprintf(stderr,"scenario=%u error=%s expected=%s\n",scenario,agent_err_name(error),agent_err_name(errors[scenario]));
        assert(error==errors[scenario] && !borrowed && !context.prompt_locked);
        assert(!strstr(spoken,"READY") && !strstr(spoken,"Temporary"));
        assert(!strstr(output,"READY") && !strstr(output,"Temporary"));
        if(scenario==INVALID_TOOL || scenario==USB_STREAM || scenario==USB_NONSTREAM ||
           scenario==PROGRESS_JOIN_FAIL || scenario==PROGRESS_JOIN_CANCEL)assert(!begins && !ends);
        else assert(begins==1 && ends==1);
        if(!error) {
            assert(turns_saved==1);
            if(scenario!=USB_STREAM && scenario!=USB_NONSTREAM)assert(early && spoken_used && !strcmp(spoken,engine.workspace->reply.text));
        } else assert(!turns_saved);
        if(scenario==ONE_TOOL)assert(effects==1 && requests==3);
        if(scenario==FOUR_TOOLS)assert(effects==4 && core.tool_rounds==4 && requests==5);
        if(scenario==INVALID_TOOL)assert(!effects && requests==3 && !tool_joins);
        if(scenario==PROGRESS_JOIN_FAIL || scenario==PROGRESS_JOIN_CANCEL)
            assert(requests==1 && tool_joins==1 && !effects && !begins && !spoken_used && !turns_saved);
        else assert(tool_joins==effects);
        if(scenario==BACKPRESSURE)assert(pauses==2 && !strcmp(spoken,"First sentence. Last sentence."));
        if(scenario==STALL)assert(pauses==80 && !spoken_used && end_reason==AGENT_ERR_TIMEOUT);
        if(scenario==CANCEL_WAIT)assert(pauses==1 && end_reason==AGENT_ERR_CANCELLED);
        if(scenario==WRITE_FAIL || scenario==STALL || scenario==CANCEL_WAIT || scenario==BEGIN_FAIL)assert(!output_used);
        if(scenario==USB_STREAM || scenario==USB_NONSTREAM)assert(requests==1 && !strcmp(output,"Normal USB reply"));
        assert(!agent_wal_iterate(&context.wal,0,engine.buffer,AGENT_ENGINE_BUFFER_SIZE,inspect,NULL));
    }
    /* A consumer can fail after the hardware effect. Preserve that error and
     * stop the batch without replay, result persistence or a spoken success. */
    scenario=FOUR_TOOLS;reset();fail_tool_event=true;
    assert(agent_engine_turn(&engine,"USER_REQUEST",true)==AGENT_ERR_SERVER);
    assert(effects==1 && engine.effects && requests==1 && tool_joins==1);
    assert(!tool_result_appends && !turns_saved && !begins && !ends && !spoken_used);
    assert(!borrowed && !context.prompt_locked);
    agent_end_turn(&core);
    progress_checks();
    cached_owner_checks();
    final_batch_checks();
    clarification_checks();
    direct_answer_checks();
    preparation_checks();
    light_snapshot_checks();
    /* Repeated pending results never unlock another action round. Final
     * generation receives the real pending pairs and must qualify completion. */
    scenario=FOUR_TOOLS;reset();batch_hint=HINT_PENDING;
    assert(!agent_engine_turn(&engine,"USER_REQUEST",true));
    assert(requests==5 && effects==4 && core.tool_rounds==AGENT_ROUNDS_MAX);
    assert(tool_result_appends==4 && turns_saved==1 && begins==1 && ends==1);
    assert(strstr(wire,"pending") && strstr(wire,"still running") && !strstr(wire,"This is the planning stage"));
    assert(!borrowed && !context.prompt_locked);agent_end_turn(&core);
    puts("voice final stream: planning gate, real tool results, four tool rounds, first-delta delivery, UTF-8 chunks, bounded backpressure, cancel/error join, shared buffer/WAL lifetime and USB compatibility PASS");
}
