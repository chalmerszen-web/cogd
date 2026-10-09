#include "engine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FLASH_BYTES=2*1024*1024 };
typedef struct {
    unsigned char flash[FLASH_BYTES];
    agent_context_t context;
    uint64_t reserved,guard_before;
    char scratch[AGENT_REQUEST_MAX+1];
    uint64_t guard_after;
    bool fail_write;
} fixture_t;
static fixture_t a,b;
static bool forbid_alloc;
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);
void *__wrap_malloc(size_t n) {assert(!forbid_alloc);return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t m) {assert(!forbid_alloc);return __real_calloc(n,m);}
void *__wrap_realloc(void *p,size_t n) {assert(!forbid_alloc);return __real_realloc(p,n);}

static agent_err_t rd(void *ctx,size_t off,void *out,size_t n)
{fixture_t *f=ctx;assert(off+n<=FLASH_BYTES);memcpy(out,f->flash+off,n);return AGENT_OK;}
static agent_err_t wr(void *ctx,size_t off,const void *in,size_t n)
{
    fixture_t *f=ctx;assert(off+n<=FLASH_BYTES);
    if(f->fail_write)return AGENT_ERR_STORAGE;
    for(size_t i=0;i<n;++i)f->flash[off+i]&=((const unsigned char *)in)[i];
    return AGENT_OK;
}
static agent_err_t er(void *ctx,size_t off,size_t n)
{fixture_t *f=ctx;assert(off+n<=FLASH_BYTES && !(off%4096) && !(n%4096));memset(f->flash+off,255,n);return AGENT_OK;}
static agent_err_t reserve(void *ctx,uint64_t *first,uint64_t *last)
{fixture_t *f=ctx;*first=f->reserved+1;f->reserved+=128;*last=f->reserved;return AGENT_OK;}
static void boot(fixture_t *f)
{
    memset(f,0,sizeof(*f));memset(f->flash,255,sizeof(f->flash));
    f->guard_before=0x123456789abcdef0ull;f->guard_after=0xfedcba9876543210ull;
    f->context=(agent_context_t){.device="prefix-test",.user="user",.session="session",
        .scratch=f->scratch,.capacity=sizeof(f->scratch),.reserve=reserve,.reserve_ctx=f};
    const agent_flash_ops_t flash={rd,wr,er,f,FLASH_BYTES,4096};
    agent_wal_t wal;assert(!agent_wal_format(&wal,&flash));
    assert(!agent_context_open(&f->context,&flash));
}
static void guards(const fixture_t *f)
{assert(f->guard_before==0x123456789abcdef0ull && f->guard_after==0xfedcba9876543210ull);}

typedef struct {
    char data[AGENT_HTTP_REQUEST_MAX+1];size_t used,calls,fail_at;
    agent_err_t error;
} sink_t;
static sink_t one,two;
static agent_err_t collect(void *ctx,const char *data,size_t n)
{
    sink_t *s=ctx;++s->calls;assert(n<=4096);
    if(s->calls==s->fail_at)return s->error;
    assert(n<sizeof(s->data)-s->used);memcpy(s->data+s->used,data,n);s->used+=n;s->data[s->used]=0;
    return AGENT_OK;
}
static agent_messages_t messages;
static void prefix_checks(void)
{
    boot(&a);agent_context_t *c=&a.context;
    strcpy(c->memory[0].key,"记忆\"\\");strcpy(c->memory[0].value,"value\n粤語🙂");
    strcpy(c->memory[1].key,"deleted");strcpy(c->memory[1].value,"must not appear");c->memory[1].deleted=true;
    strcpy(c->summary.text,"earlier\ttext\r\nquoted\"");strcpy(c->summary.device,"source");c->summary.through=123;
    const char *system="system with \"quote\", newline\n and 中文🙂";
    strcpy(c->scratch,system);assert(!agent_context_prompt(c,&messages,c->scratch));
    strcpy(c->scratch,system);memset(&one,0,sizeof(one));
    forbid_alloc=true;assert(!agent_context_write_prefix(c,c->scratch,collect,&one));forbid_alloc=false;
    size_t calls=one.calls;assert(!collect(&one,"]",1));
    assert(!strcmp(one.data,messages.data) && messages.used==messages.system_used);
    assert(!strstr(one.data,"must not appear"));
    cJSON *parsed=agent_json_parse(one.data,one.used);assert(parsed && cJSON_GetArraySize(parsed)==3);cJSON_Delete(parsed);
    for(size_t fail=1;fail<=calls;++fail) {
        strcpy(c->scratch,system);memset(&two,0,sizeof(two));two.fail_at=fail;two.error=AGENT_ERR_CANCELLED;
        forbid_alloc=true;agent_err_t error=agent_context_write_prefix(c,c->scratch,collect,&two);forbid_alloc=false;
        assert(error==AGENT_ERR_CANCELLED && two.calls==fail);
        assert(two.used<one.used && !memcmp(one.data,two.data,two.used));guards(&a);
    }
    boot(&a);c=&a.context;
    static char boundary[AGENT_HISTORY_MAX+1];
    size_t n=AGENT_HISTORY_MAX-strlen("[{\"role\":\"system\",\"content\":\"\"}]");
    memset(boundary,'x',n);boundary[n]=0;
    assert(!agent_context_prompt(c,&messages,boundary) && messages.used==AGENT_HISTORY_MAX);
    memset(&one,0,sizeof(one));assert(!agent_context_write_prefix(c,boundary,collect,&one));
    assert(one.used==AGENT_HISTORY_MAX-1 && !collect(&one,"]",1) && !strcmp(one.data,messages.data));
    boundary[n++]='x';boundary[n]=0;
    assert(agent_context_prompt(c,&messages,boundary)==AGENT_ERR_LIMIT);
    memset(&one,0,sizeof(one));assert(agent_context_write_prefix(c,boundary,collect,&one)==AGENT_ERR_LIMIT);
    assert(agent_context_write_prefix(NULL,system,collect,&one)==AGENT_ERR_ARGUMENT);
    assert(agent_context_write_prefix(c,system,NULL,&one)==AGENT_ERR_ARGUMENT);guards(&a);
}
static void same_persistence(void)
{
    assert(!memcmp(a.flash,b.flash,FLASH_BYTES));
    assert(a.context.events==b.context.events && a.context.pending==b.context.pending);
    assert(a.context.lamport==b.context.lamport && a.context.next_seq==b.context.next_seq);
    assert(a.context.wal.next_seq==b.context.wal.next_seq);guards(&a);guards(&b);
}
static void user_checks(void)
{
    static char text[AGENT_INPUT_MAX+2],content[AGENT_HISTORY_MAX+1];
    for(unsigned variant=0;variant<5;++variant) {
        boot(&a);boot(&b);
        if(variant==0)strcpy(text,"你好，小言。\nquote\" slash\\\t🙂");
        else {memset(text,variant==1?'x':1,AGENT_INPUT_MAX);text[AGENT_INPUT_MAX]=0;}
        agent_json_writer_t w;agent_json_writer_init(&w,content,sizeof(content));
        agent_json_raw(&w,"{\"format\":\"text/plain\",\"text\":");agent_json_quote(&w,text);agent_json_raw(&w,"}");assert(!w.error);
        char first[64]="unchanged",second[64]="unchanged";
        a.fail_write=b.fail_write=variant==3;
        size_t cap=variant==4?1:sizeof(first);
        agent_err_t old=agent_context_emit(&a.context,"message","user",content,first,cap);
        agent_err_t now=agent_context_emit_user(&b.context,text,second,cap);
        assert(old==now && !strcmp(first,second));
        assert(old==(variant==3?AGENT_ERR_STORAGE:variant==4?AGENT_ERR_LIMIT:AGENT_OK));same_persistence();
    }
    boot(&a);agent_context_t saved=a.context;
    memset(text,'x',AGENT_INPUT_MAX+1);text[AGENT_INPUT_MAX+1]=0;
    assert(agent_context_emit_user(&a.context,text,NULL,0)==AGENT_ERR_LIMIT);
    assert(agent_context_emit_user(&a.context,"",NULL,0)==AGENT_ERR_LIMIT);
    assert(agent_context_emit_user(&a.context,"\xc0\x80",NULL,0)==AGENT_ERR_LIMIT);
    strcpy(a.scratch,"aliased");
    assert(agent_context_emit_user(&a.context,a.scratch,NULL,0)==AGENT_ERR_ARGUMENT);
    assert(!strcmp(a.scratch,"aliased") && !memcmp(&saved,&a.context,sizeof(saved)));
    /* Existing append semantics retain a received input even if the scan
     * cancellation flag is set. Request cancellation is enforced separately;
     * the new serializer must not silently change durable-input semantics. */
    boot(&a);boot(&b);atomic_bool cancel=true;
    a.context.cancelled=b.context.cancelled=&cancel;
    assert(!agent_context_emit(&a.context,"message","user","{\"format\":\"text/plain\",\"text\":\"valid\"}",NULL,0));
    assert(!agent_context_emit_user(&b.context,"valid",NULL,0));same_persistence();
}
static agent_err_t prefix_body(void *ctx,agent_write_fn write,void *write_ctx)
{
    agent_context_t *c=ctx;
    /* No engine/message/reply workspace is present in this producer. */
    agent_err_t e=agent_context_write_prefix(c,"fixed system",write,write_ctx);
    if(!e)e=agent_context_replay(c,write,write_ctx);
    if(!e)e=write(write_ctx,",",1);
    if(!e)e=agent_message_write("user","完整输入\n不对，改为绿色。",NULL,write,write_ctx);
    return e?e:write(write_ctx,"]",1);
}
static void history_checks(void)
{
    boot(&a);agent_context_t *c=&a.context;
    char text[1700],content[4096];memset(text,'h',sizeof(text)-1);text[sizeof(text)-1]=0;
    agent_json_writer_t w;agent_json_writer_init(&w,content,sizeof(content));
    agent_json_raw(&w,"{\"messages\":[{\"role\":\"user\",\"content\":");agent_json_quote(&w,text);
    agent_json_raw(&w,"},{\"role\":\"assistant\",\"content\":\"paired reply\"}]}");assert(!w.error);
    for(unsigned i=0;i<128;++i)assert(!agent_context_emit(c,"turn","assistant",content,NULL,0));
    assert(!agent_context_select(c,c->wal.next_seq,NULL) && c->prompt_bytes>190*1024);
    assert(!agent_context_prompt(c,&messages,"fixed system"));
    assert(!agent_messages_add(&messages,"user","完整输入\n不对，改为绿色。",NULL));
    memset(&one,0,sizeof(one));size_t boundary=messages.system_used-1;
    assert(!collect(&one,messages.data,boundary));assert(!agent_context_replay(c,collect,&one));
    assert(!collect(&one,messages.data+boundary,messages.used-boundary));
    memset(&two,0,sizeof(two));assert(!prefix_body(c,collect,&two));
    assert(two.used==one.used && !memcmp(one.data,two.data,one.used));
    size_t measured=0;assert(!agent_http_measure_body(prefix_body,c,NULL,&measured) && measured==one.used);
    assert(agent_context_emit_user(c,"locked",NULL,0)==AGENT_ERR_BUSY);
    atomic_bool cancel=true;c->prompt_cancel=&cancel;
    size_t cancelled_bytes=0;
    assert(agent_http_measure_body(prefix_body,c,&cancel,&cancelled_bytes)==AGENT_ERR_CANCELLED);
    assert(!cancelled_bytes);
    memset(&two,0,sizeof(two));assert(prefix_body(c,collect,&two)==AGENT_ERR_CANCELLED);
    c->prompt_cancel=NULL;
    size_t bad=c->recent[c->prompt_start].offset+32;a.flash[bad]^=1;
    memset(&two,0,sizeof(two));assert(prefix_body(c,collect,&two)==AGENT_ERR_CORRUPT);a.flash[bad]^=1;
    agent_context_release(c);guards(&a);
    printf("Streamed prefix/history: %zu identical bytes, original200KiB history budget preserved\n",measured);
}
static unsigned appended;
static agent_err_t custom_append(agent_context_t *c,const char *type,const char *actor,const char *content,char *id,size_t cap)
{
    if(!strcmp(type,"message") && !strcmp(actor,"user")) {
        ++appended;cJSON *root=agent_json_parse(content,strlen(content));assert(root);
        assert(!strcmp(agent_json_string(root,"format"),"text/plain"));
        assert(!strcmp(agent_json_string(root,"text"),"custom input"));cJSON_Delete(root);
    }
    return agent_context_emit(c,type,actor,content,id,cap);
}
static uint64_t now(void *ctx) {(void)ctx;return 1000;}
static agent_err_t reply(void *ctx,const agent_http_request_t *r,agent_http_feed_fn feed,void *feed_ctx)
{
    (void)ctx;memset(&one,0,sizeof(one));assert(!agent_http_write_body(r,collect,&one));
    const char *json="{\"choices\":[{\"message\":{\"content\":\"OK\"},\"finish_reason\":\"stop\"}]}";
    return feed(feed_ctx,json,strlen(json));
}
static void custom_checks(void)
{
    boot(&a);static agent_engine_storage_t storage;agent_core_t core;agent_core_init(&core);
    const agent_transport_ops_t transport={reply,NULL};
    const agent_llm_ops_t llm={.compose=agent_deepseek_compose,.parse=agent_llm_parse};
    const agent_tool_ops_t tools={.now_ms=now};agent_context_ops_t ops=agent_context_ops;ops.append=custom_append;
    agent_engine_t engine={AGENT_ENGINE_STORAGE_INIT(storage),.core=&core,.context=&a.context,
        .context_ops=&ops,.tools=&tools,.llm=&llm,.transport=&transport};
    assert(!agent_engine_bind_workspace(&engine,&storage,sizeof(storage)));
    assert(!agent_begin_turn(&core));assert(!agent_engine_turn(&engine,"custom input",false));agent_end_turn(&core);
    assert(appended==1 && a.context.events==2);
}
int main(void)
{
    prefix_checks();user_checks();history_checks();custom_checks();
    puts("No-heap prefix, exact size/error boundaries, user WAL equivalence/failures, alias and custom adapter PASS");
}
