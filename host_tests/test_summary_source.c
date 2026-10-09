#include "context.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char flash[2u*1024u*1024u];
static char scratch[AGENT_REQUEST_MAX+1];
static agent_context_t context;
static uint64_t reserved,clock_ms;
static unsigned reads,writes;
static bool fail_read,expire_read,cancel_read;
static atomic_bool cancelled;
static size_t allocation_max,allocation_failure;
static bool measure_allocations;
void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size)
{
    if(measure_allocations && size>allocation_max)allocation_max=size;
    if(allocation_failure && size==allocation_failure) {allocation_failure=0;return NULL;}
    return __real_malloc(size);
}
static agent_err_t rd(void *ctx,size_t off,void *data,size_t n)
{
    (void)ctx;++reads;assert(off+n<=sizeof(flash));
    if(fail_read)return AGENT_ERR_STORAGE;
    if(expire_read)clock_ms+=AGENT_CONTEXT_SCAN_MS+1;
    if(cancel_read)atomic_store(&cancelled,true);
    memcpy(data,flash+off,n);return AGENT_OK;
}
static agent_err_t wr(void *ctx,size_t off,const void *data,size_t n)
{
    (void)ctx;++writes;assert(off+n<=sizeof(flash));
    const unsigned char *p=data;
    for(size_t i=0;i<n;++i) {assert((flash[off+i]&p[i])==p[i]);flash[off+i]&=p[i];}
    return AGENT_OK;
}
static agent_err_t er(void *ctx,size_t off,size_t n)
{
    (void)ctx;assert(!(off%4096) && !(n%4096) && off+n<=sizeof(flash));
    memset(flash+off,255,n);return AGENT_OK;
}
static const agent_flash_ops_t storage={rd,wr,er,NULL,sizeof(flash),4096};
static agent_err_t reserve(void *ctx,uint64_t *first,uint64_t *last)
{ (void)ctx;*first=reserved+1;reserved+=128;*last=reserved;return AGENT_OK; }
static uint64_t now(void *ctx) { (void)ctx;return clock_ms; }
static void boot(void)
{
    memset(&context,0,sizeof(context));
    context.device="local";context.user="user";context.session="session";
    context.scratch=scratch;context.capacity=sizeof(scratch);context.reserve=reserve;
    assert(!agent_context_open(&context,&storage));
    context.now_ms=now;context.cancelled=&cancelled;
}
static void reject(agent_err_t expected,uint64_t through)
{
    size_t events=context.events;unsigned before=writes;
    assert(agent_context_summary_set(&context,"must not persist",through)==expected);
    assert(context.events==events && writes==before);
}
static void summary_allocation(uint64_t through)
{
    const char *patterns[]={"a","中文","\xf0\x9f\x8e\xb5","\001\n\t\"\\","中A\"\n\xf0\x9f\x8e\xb5"};
    for(unsigned i=0;i<sizeof(patterns)/sizeof(*patterns);++i) {
        char text[AGENT_SUMMARY_MAX+1],encoded[AGENT_SUMMARY_MAX*6+3];
        size_t unit=strlen(patterns[i]),used=0;
        while(used+unit<=AGENT_SUMMARY_MAX) {memcpy(text+used,patterns[i],unit);used+=unit;}
        text[used]=0;
        agent_json_writer_t writer;agent_json_writer_init(&writer,encoded,sizeof(encoded));
        agent_json_quote(&writer,text);assert(!writer.error);
        size_t expected=80+writer.used-2;
        allocation_max=0;measure_allocations=true;
        assert(!agent_context_summary_set(&context,text,through));
        measure_allocations=false;
        assert(allocation_max==expected && !strcmp(context.summary.text,text));
        if(i<3)assert(allocation_max<=AGENT_SUMMARY_MAX+80);
        boot();assert(!strcmp(context.summary.text,text) && context.summary.through==through);
        agent_summary_t saved=context.summary;size_t events=context.events;unsigned writes_before=writes;
        allocation_failure=expected;
        assert(agent_context_summary_set(&context,text,through)==AGENT_ERR_MEMORY);
        assert(!allocation_failure && context.events==events && writes==writes_before &&
               !memcmp(&context.summary,&saved,sizeof(saved)));
        printf("Summary allocation pattern=%u: text=%zu allocated=%zu previous_upper=%zu\n",i,used,expected,used*6+80);
    }
}
int main(void)
{
    memset(flash,255,sizeof(flash));
    assert(!agent_wal_format(&context.wal,&storage));boot();
    const char *turn="{\"messages\":[{\"role\":\"user\",\"content\":\"保留完整历史\"},"
        "{\"role\":\"assistant\",\"content\":\"这是已提交的对话\"}]}";
    uint64_t first=0;
    for(unsigned i=0;i<150;++i) {
        assert(!agent_context_emit(&context,"turn","assistant",turn,NULL,0));
        if(!i)first=context.last_turn_seq;
    }
    uint64_t latest=context.last_turn_seq;
    assert(context.recent_count==AGENT_RECENT_MAX);
    reads=0;
    assert(!agent_context_summary_set(&context,"newest",0));
    unsigned indexed=reads;
    assert(context.summary.through==latest && indexed<16);
    reads=0;
    assert(!agent_context_summary_set(&context,"older than index",first));
    unsigned fallback=reads;
    assert(context.summary.through==first && fallback>indexed*30);
    reject(AGENT_ERR_NOT_FOUND,context.last_local+1000);

    /* A newer peer turn with the same device sequence is not a local source. */
    char remote[1024];
    snprintf(remote,sizeof(remote),"{\"schema\":\"agent.context.event/1\","
        "\"event_id\":\"peer:%020llu\",\"device_id\":\"peer\",\"device_seq\":%llu,"
        "\"user_id\":\"user\",\"session_id\":\"session\",\"lamport\":%llu,"
        "\"type\":\"turn\",\"actor\":{},\"policy\":{},\"parents\":[],\"content\":%s}",
        (unsigned long long)latest,(unsigned long long)latest,
        (unsigned long long)(context.lamport+1),turn);
    cJSON *event=agent_json_parse(remote,strlen(remote));assert(event);
    assert(!agent_context_ingest(&context,event));cJSON_Delete(event);
    reads=0;assert(!agent_context_summary_set(&context,"local identity",latest));
    assert(context.summary.through==latest && !strcmp(context.summary.device,"local") && reads<32);

    atomic_store(&cancelled,true);reads=0;
    reject(AGENT_ERR_CANCELLED,latest);assert(!reads);
    atomic_store(&cancelled,false);
    fail_read=true;reject(AGENT_ERR_STORAGE,latest);fail_read=false;
    expire_read=true;reject(AGENT_ERR_TIMEOUT,latest);expire_read=false;
    cancel_read=true;reject(AGENT_ERR_CANCELLED,latest);cancel_read=false;
    atomic_store(&cancelled,false);
    context.wal.ready=false;reject(AGENT_ERR_STORAGE,latest);context.wal.ready=true;

    /* CRC failures and stale descriptors must not be retried as cache misses. */
    agent_record_t *source=&context.recent[context.recent_count-2];
    size_t bad=source->offset+32+12;
    flash[bad]^=1;reject(AGENT_ERR_CORRUPT,latest);flash[bad]^=1;
    ++source->seq;reject(AGENT_ERR_CORRUPT,latest);--source->seq;

    size_t events=context.events;
    boot();assert(context.events==events && context.recent_count==AGENT_RECENT_MAX);
    assert(!agent_context_summary_set(&context,"after reboot",0));
    assert(context.summary.through==latest);
    size_t base=context.wal.base;
    assert(!agent_context_compact(&context));assert(context.wal.base!=base);
    reads=0;assert(!agent_context_summary_set(&context,"after rotation",0));
    assert(context.summary.through==latest && reads<32);
    boot();assert(context.summary.through==latest && !strcmp(context.summary.text,"after rotation"));
    assert(!agent_context_summary_set(&context,"retained old source",first));
    assert(context.summary.through==first);
    printf("Summary source: indexed %u reads, retained-source fallback %u; identity, CRC, faults, cancellation, deadlines, reboot and rotation PASS\n",indexed,fallback);
    summary_allocation(latest);
}
