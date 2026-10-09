#include "engine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { NORMAL, LEGACY, CONNECT_FAIL, SEND_FAIL, RESPONSE_FAIL, CANCEL_SEND,
       RESUME_FAIL, CANCEL_RESUME, BAD_RESPONSE, EMPTY_RESPONSE, BIG_SNAPSHOT,
       CUSTOM_CONTEXT, BAD_INPUT, WAL_FAIL, CORRUPT_HISTORY, LARGE_SYSTEM,
       MAX_ESCAPE, REPLACE_FULL, CASES };
static unsigned char flash[2*1024*1024],saved_flash[sizeof(flash)];
static agent_engine_workspace_t state;
static _Alignas(8) char buffer[AGENT_ENGINE_BUFFER_SIZE];
static _Alignas(8) char replacement[AGENT_ENGINE_BUFFER_SIZE];
#if AGENT_REQUEST_SCRATCH_COMPACT
static _Alignas(8) char preparation[16384+16];
static size_t preparation_capacity;
#endif
static bool compact;
static char hardware_prompt[8001];
static agent_engine_t engine;
static agent_context_t context;
static agent_core_t core;
static agent_engine_deferred_t staging;
static char wire[AGENT_HTTP_REQUEST_MAX+1],gold[2][sizeof(wire)],input[AGENT_INPUT_MAX+1];
static size_t used;
static uint64_t reserved;
static unsigned scenario,requests,resumes,effects,display_reads,light_reads,begins;
static bool deferred,body_complete,fail_wal;
static uint8_t current_rgb[3];

static agent_err_t rd(void *p,size_t off,void *out,size_t n)
{ (void)p;assert(off+n<=sizeof(flash));memcpy(out,flash+off,n);return AGENT_OK; }
static agent_err_t wr(void *p,size_t off,const void *in,size_t n)
{
    (void)p;if(fail_wal)return AGENT_ERR_STORAGE;assert(off+n<=sizeof(flash));
    for(size_t i=0;i<n;++i) {assert((flash[off+i]&((const unsigned char *)in)[i])==((const unsigned char *)in)[i]);flash[off+i]&=((const unsigned char *)in)[i];}
    return AGENT_OK;
}
static agent_err_t er(void *p,size_t off,size_t n)
{ (void)p;assert(!(off%4096) && !(n%4096) && off+n<=sizeof(flash));memset(flash+off,255,n);return AGENT_OK; }
static const agent_flash_ops_t storage={rd,wr,er,NULL,sizeof(flash),4096};
static agent_err_t reserve(void *p,uint64_t *first,uint64_t *last)
{ (void)p;*first=reserved+1;reserved+=128;*last=reserved;return AGENT_OK; }
static uint64_t now(void *p) {(void)p;return 1000;}
static void pause_ms(unsigned n) {(void)n;}
static agent_err_t display(void *p,char *out,size_t cap)
{
    (void)p;++display_reads;
    if(scenario==BIG_SNAPSHOT) {assert(cap>1800);memset(out,'s',1800);out[1800]=0;}
    else snprintf(out,cap,"{\"mode\":\"clock\",\"frame\":%u}",display_reads);
    return AGENT_OK;
}
static agent_err_t light_get(void *p,uint8_t rgb[3])
{ (void)p;++light_reads;memcpy(rgb,current_rgb,3);return AGENT_OK; }
static agent_err_t light_set(void *p,const uint8_t rgb[3])
{ (void)p;assert(engine.workspace && (!deferred || resumes==1));assert(rgb[0]==4 && rgb[1]==2 && rgb[2]==1);++effects;return AGENT_OK; }
static agent_err_t resume(void *p,agent_err_t result)
{
    (void)p;assert(++resumes==1 && !context.prompt_locked && !engine.workspace);
    assert(context.scratch==engine.buffer);
#if AGENT_REQUEST_SCRATCH_COMPACT
    if(compact)assert(engine.buffer==preparation && context.capacity==preparation_capacity);
    else
#endif
        assert(engine.buffer==buffer && context.capacity==sizeof(buffer));
    if(scenario==RESUME_FAIL)return AGENT_ERR_MEMORY;
    assert(!agent_engine_bind_parts(&engine,&state,sizeof(state),
        scenario==REPLACE_FULL?replacement:buffer,sizeof(buffer)));
#if AGENT_REQUEST_SCRATCH_COMPACT
    if(compact)memset(preparation,0xdd,preparation_capacity);
#endif
    if(scenario==CANCEL_RESUME) {agent_cancel(&core);return AGENT_ERR_CANCELLED;}
    return result;
}
static agent_err_t answer_begin(void *p)
{
    (void)p;assert(engine.workspace && (!deferred || resumes==1) && effects==1);
    ++begins;strcpy(input,"ASR storage reused only after request copied");return AGENT_OK;
}
static agent_err_t answer_write(void *p,const char *text,size_t n)
{ (void)p;assert(n && agent_utf8_valid(text,n) && begins==1);return AGENT_OK; }
static agent_err_t answer_end(void *p,agent_err_t result) {(void)p;return result;}
static agent_err_t custom_append(agent_context_t *c,const char *type,const char *actor,const char *text,char *id,size_t cap)
{ return agent_context_emit(c,type,actor,text,id,cap); }
static agent_err_t collect(void *p,const char *data,size_t n)
{
    (void)p;assert(n<=4096 && used+n<sizeof(wire));
    if(scenario==SEND_FAIL && used>200)return AGENT_ERR_NETWORK;
    if(scenario==CANCEL_SEND && used>200)agent_cancel(&core);
    memcpy(wire+used,data,n);used+=n;wire[used]=0;return AGENT_OK;
}
static agent_err_t http(void *p,const agent_http_request_t *r,agent_http_feed_fn feed,void *ctx)
{
    (void)p;++requests;assert(requests<=2);used=0;
    bool early=scenario==BIG_SNAPSHOT || scenario==CUSTOM_CONTEXT;
#if AGENT_REQUEST_SCRATCH_COMPACT
    early=early || (compact && scenario==LARGE_SYSTEM && preparation_capacity==8192);
#endif
    if(deferred && requests==1 && !early)
        assert(!resumes && !engine.workspace && context.prompt_locked);
    if(scenario==CONNECT_FAIL)return AGENT_ERR_DNS;
    current_rgb[0]=99; /* Measuring/sending/rebuilding must not re-read this. */
    size_t corrupt=context.recent[context.prompt_start].offset+32;
    if(scenario==CORRUPT_HISTORY)flash[corrupt]^=1;
    agent_err_t error=agent_http_write_body(r,collect,NULL);
    if(scenario==CORRUPT_HISTORY)flash[corrupt]^=1;
    if(error)return error;
    assert(used==r->length && used>190*1024 && context.history_budget==200*1024);
    if(scenario==NORMAL || scenario==LEGACY || scenario==BIG_SNAPSHOT || scenario==CUSTOM_CONTEXT ||
       scenario==LARGE_SYSTEM || scenario==MAX_ESCAPE) {
        if(!deferred)strcpy(gold[requests-1],wire);
        else assert(!strcmp(gold[requests-1],wire));
        const char *dir=getenv("AGENT_HANDOFF_CAPTURE_DIR");
        if(dir) {
            char path[512];snprintf(path,sizeof(path),"%s/%u-%s-%u.json",dir,scenario,deferred?"deferred":"ordinary",requests);
            FILE *f=fopen(path,"wb");assert(f);assert(fwrite(wire,1,used,f)==used);assert(!fclose(f));
        }
    }
    const char *end=NULL;cJSON *parsed=cJSON_ParseWithLengthOpts(wire,used,&end,false);
    assert(parsed && end==wire+used);cJSON_Delete(parsed);
    if(scenario!=BIG_SNAPSHOT)assert(display_reads==1);
    assert(light_reads==1);body_complete=true;
    if(scenario!=LEGACY && r->on_sent) {error=r->on_sent(r->sent_ctx);if(error)return error;}
    if(!(deferred && requests==1 && scenario==LEGACY))
        assert(engine.workspace && context.capacity==sizeof(buffer));
    if(scenario==RESPONSE_FAIL)return AGENT_ERR_NETWORK;
    if(scenario==EMPTY_RESPONSE)return AGENT_OK;
    if(scenario==BAD_RESPONSE)return feed(ctx,"data: {BAD}\n\n",13);
    const char *response=requests==1?
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"full-call-粤語\",\"type\":\"function\",\"function\":{\"name\":\"device_light_set_rgb\",\"arguments\":\"{\\\"r\\\":4,\\\"g\\\":2,\\\"b\\\":1}\"}}]},\"finish_reason\":\"tool_calls\"}]}\n\ndata: [DONE]\n\n":
        "data: {\"choices\":[{\"delta\":{\"content\":\"ANSWER:已经完成。\"},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n";
    unsigned seed=17;
    for(size_t at=0,n=strlen(response);at<n;) {
        seed=seed*1664525u+1013904223u;size_t chunk=1+seed%31;if(chunk>n-at)chunk=n-at;
        error=feed(ctx,response+at,chunk);if(error)return error;at+=chunk;
    }
    return AGENT_OK;
}
static const agent_transport_ops_t transport={http,NULL};
static const agent_llm_ops_t llm={.compose=agent_deepseek_compose,.compose_final=agent_deepseek_compose_final,.parse=agent_llm_parse};
static const agent_display_ops_t display_ops={.status=display};
static agent_tool_ops_t tools={.now_ms=now,.display=&display_ops,.light_get=light_get,.light_set=light_set};

static agent_err_t run(bool delay)
{
    deferred=delay;body_complete=fail_wal=false;
    requests=resumes=effects=display_reads=light_reads=begins=0;reserved=0;memset(current_rgb,0,3);
    memset(flash,255,sizeof(flash));memset(&state,0,sizeof(state));agent_core_init(&core);
    context=(agent_context_t){.device="handoff",.user="user",.session="session",.reserve=reserve,.scratch=buffer,.capacity=sizeof(buffer)};
    static char identity[65];
    if(scenario==MAX_ESCAPE) {
        memset(identity,1,sizeof(identity)-1);identity[64]=0;
        context.user=context.session=identity;context.device="01234567890123456789012345678901";
        reserved=AGENT_SEQ_MAX-1024;
    }
    agent_wal_t wal;assert(!agent_wal_format(&wal,&storage));assert(!agent_context_open(&context,&storage));
    char text[1700],content[4096];memset(text,'h',sizeof(text)-1);text[sizeof(text)-1]=0;
    snprintf(content,sizeof(content),"{\"messages\":[{\"role\":\"user\",\"content\":\"%s\"},{\"role\":\"assistant\",\"content\":\"paired reply\"}]}",text);
    for(unsigned i=0;i<128;++i)assert(!agent_context_emit(&context,"turn","assistant",content,NULL,0));
    static agent_context_ops_t ops;ops=agent_context_ops;if(scenario==CUSTOM_CONTEXT)ops.append=custom_append;
    engine=(agent_engine_t){.core=&core,.context=&context,.context_ops=&ops,.tools=&tools,.llm=&llm,
        .transport=&transport,.voice_mode=true,.answer_begin=answer_begin,.answer_write=answer_write,
        .answer_end=answer_end,.pause_ms=pause_ms};
    strcpy(input,"请把灯改成蓝色。不对，改成绿色。完整普通话与粤語🙂\n");
    size_t size=strlen(input);memset(input+size,'a',AGENT_INPUT_MAX-size);input[AGENT_INPUT_MAX]=0;
    if(scenario==MAX_ESCAPE) {memset(input,1,AGENT_INPUT_MAX);input[AGENT_INPUT_MAX]=0;context.lamport=AGENT_SEQ_MAX-8;}
    memset(hardware_prompt,'p',sizeof(hardware_prompt)-1);hardware_prompt[8000]=0;
    tools.hardware_prompt=scenario==LARGE_SYSTEM?hardware_prompt:NULL;
    if(scenario==BAD_INPUT)input[0]=0;
    if(delay) {
#if AGENT_REQUEST_SCRATCH_COMPACT
        if(compact) {
            memset(preparation,0xa5,sizeof(preparation));
            assert(agent_engine_bind_buffer(&engine,preparation,preparation_capacity)==AGENT_ERR_ARGUMENT);
            assert(!agent_engine_bind_preparation_buffer(&engine,preparation,preparation_capacity));
        } else
#endif
            assert(!agent_engine_bind_buffer(&engine,buffer,sizeof(buffer)));
    }
    else assert(!agent_engine_bind_parts(&engine,&state,sizeof(state),buffer,sizeof(buffer)));
    staging=(agent_engine_deferred_t){.resume=resume};
    assert(!agent_begin_turn(&core));fail_wal=scenario==WAL_FAIL;
    agent_err_t error=delay?agent_engine_turn_deferred(&engine,input,true,&staging):agent_engine_turn(&engine,input,true);
    agent_end_turn(&core);
    assert(!context.prompt_locked && (!delay || resumes==1));
    assert(context.history_budget==200*1024 && context.scratch==engine.buffer);
#if AGENT_REQUEST_SCRATCH_COMPACT
    if(compact)for(size_t i=preparation_capacity;i<sizeof(preparation);++i)assert((unsigned char)preparation[i]==0xa5);
#endif
    if(!error)assert(requests==2 && effects==1 && begins==1 && context.events==131);
    else assert(requests<=1 && !effects && !begins);
    return error;
}
int main(void)
{
    compact=false;
    for(scenario=0;scenario<CASES;++scenario) {
        printf("deferred case %u\n",scenario);fflush(stdout);
        if(scenario==NORMAL || scenario==LEGACY || scenario==BIG_SNAPSHOT || scenario==CUSTOM_CONTEXT ||
           scenario==LARGE_SYSTEM || scenario==MAX_ESCAPE) {
            assert(!run(false));memcpy(saved_flash,flash,sizeof(flash));
            assert(!run(true));assert(!memcmp(flash,saved_flash,sizeof(flash)));
#if AGENT_REQUEST_SCRATCH_COMPACT
            compact=true;
            preparation_capacity=scenario==MAX_ESCAPE?16384:8192;
            assert(!run(true));assert(!memcmp(flash,saved_flash,sizeof(flash)));
            preparation_capacity=16384;
            assert(!run(true));assert(!memcmp(flash,saved_flash,sizeof(flash)));
            compact=false;
#endif
        } else assert(run(true)!=AGENT_OK);
#if AGENT_REQUEST_SCRATCH_COMPACT
        if(scenario!=REPLACE_FULL && scenario!=NORMAL && scenario!=LEGACY && scenario!=BIG_SNAPSHOT &&
           scenario!=CUSTOM_CONTEXT && scenario!=LARGE_SYSTEM && scenario!=MAX_ESCAPE) {
            compact=true;preparation_capacity=8192;
            assert(run(true)!=AGENT_OK);compact=false;
        }
#endif
    }
    puts("Deferred first request: identical two-request wire/WAL, 200KiB history, full2KiB input, frozen snapshots, one join, legacy and faults PASS");
}
