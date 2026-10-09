#include "engine_memory.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static unsigned calls,fail_at,live;
static size_t ceiling=45056,largest;
static void *blocks[2];
void *__real_calloc(size_t,size_t);
void __real_free(void *);
void *__wrap_calloc(size_t count,size_t size)
{
    ++calls;
    if((count && size>ceiling/count) || calls==fail_at)return NULL;
    size_t bytes=count*size;if(bytes>largest)largest=bytes;
    void *p=__real_calloc(count,size);assert(p);
    assert(live<2);blocks[live++]=p;return p;
}
void __wrap_free(void *p)
{
    if(!p)return;
    unsigned i=0;while(i<live && blocks[i]!=p)++i;
    assert(i<live);blocks[i]=blocks[--live];__real_free(p);
}
#if AGENT_REQUEST_SCRATCH_COMPACT
static void sized(agent_engine_t *e,agent_context_t *c,const char *input,size_t expected)
{
    calls=fail_at=0;ceiling=SIZE_MAX;largest=0;
    assert(!esp_agent_engine_memory_prepare_input(e,input));
    assert(live==1 && calls==1 && largest==expected && c->capacity==expected && !e->workspace);
    assert(e->preparation_capacity==(expected==AGENT_ENGINE_BUFFER_SIZE?0:expected));
    assert(!esp_agent_engine_memory_release(e) && !live && !c->scratch);
}
static void compact_cases(agent_engine_t *e,agent_context_t *c)
{
    c->wal.ready=true;c->wal.bank_size=1024*1024;c->wal.used=64;
    sized(e,c,"short",8192);
    static char escaped[AGENT_INPUT_MAX+1];memset(escaped,1,AGENT_INPUT_MAX);
    sized(e,c,escaped,16384);
    c->recent_count=1;c->recent[0].length=8191;c->recent_message_bytes[0]=100;
    sized(e,c,"short",8192);
    c->recent[0].length=8192;sized(e,c,"short",16384);
    c->recent[0].length=16383;sized(e,c,"short",16384);
    c->recent[0].length=16384;sized(e,c,"short",AGENT_ENGINE_BUFFER_SIZE);
    c->recent[0].length=100;c->recent_message_bytes[0]=8187;sized(e,c,"short",8192);
    c->recent_message_bytes[0]=8188;sized(e,c,"short",16384);
    c->recent_message_bytes[0]=16379;sized(e,c,"short",16384);
    c->recent_message_bytes[0]=16380;sized(e,c,"short",AGENT_ENGINE_BUFFER_SIZE);
    c->recent_message_bytes[0]=0;sized(e,c,"short",AGENT_ENGINE_BUFFER_SIZE);
    c->recent_count=0;c->wal.tail_recovered=true;sized(e,c,"short",AGENT_ENGINE_BUFFER_SIZE);
    c->wal.tail_recovered=false;c->wal.used=c->wal.bank_size-AGENT_REQUEST_MAX-31;
    sized(e,c,"short",AGENT_ENGINE_BUFFER_SIZE);
    c->wal.used=c->wal.bank_size-AGENT_REQUEST_MAX-32;sized(e,c,"short",8192);
    c->wal.used=64;
    assert(esp_agent_engine_memory_prepare_input(e,"")==AGENT_ERR_LIMIT);
    assert(esp_agent_engine_memory_prepare_input(e,"\xff")==AGENT_ERR_LIMIT && !live);
    for(unsigned adopt=0;adopt<2;++adopt)for(unsigned failure=0;failure<3;++failure) {
        calls=fail_at=0;ceiling=SIZE_MAX;
        size_t bytes=sizeof(agent_engine_workspace_t);
        unsigned char *capture=adopt?calloc(1,bytes+64):NULL;
        if(capture)memset(capture,0xa5,bytes+64);
        assert(!esp_agent_engine_memory_prepare_input(e,"short"));
        char *old=e->buffer;old[8191]=73;unsigned prior=calls;
        assert(esp_agent_engine_memory_prepare(e)==AGENT_ERR_BUSY);
        assert(!esp_agent_engine_memory_prepare_input(e,"short") && calls==prior);
        assert(esp_agent_engine_memory_prepare_input(e,escaped)==AGENT_ERR_BUSY && calls==prior);
        c->prompt_locked=true;
        assert(esp_agent_engine_memory_restore(e,false)==AGENT_ERR_BUSY);
        if(adopt)assert(esp_agent_engine_memory_adopt(e,capture,bytes)==AGENT_ERR_BUSY);
        assert(e->buffer==old && old[8191]==73 && calls==prior);
        c->prompt_locked=false;e->answer_active=true;
        assert(esp_agent_engine_memory_release(e)==AGENT_ERR_BUSY);
        e->answer_active=false;
        /* Failure1 is the replacement buffer; failure2 is full-state calloc.
         * Adopting the caller arena has no replacement state allocation. */
        fail_at=failure?calls+failure:0;
        agent_err_t error=adopt?esp_agent_engine_memory_adopt(e,capture,bytes+64):
                                esp_agent_engine_memory_restore(e,false);
        bool failed=failure==1 || (!adopt && failure==2);
        assert(error==(failed?AGENT_ERR_MEMORY:AGENT_OK));
        if(!failed) {
            assert(e->workspace && !e->preparation_capacity && c->capacity==AGENT_ENGINE_BUFFER_SIZE);
            if(capture) {
                assert(e->scratch==capture);
                for(size_t i=0;i<bytes;++i)assert(!capture[i]);
                for(size_t i=bytes;i<bytes+64;++i)assert(capture[i]==0xa5);
            }
        } else if(capture)assert(!e->workspace && capture[0]==0xa5);
        assert(!esp_agent_engine_memory_release(e));
        if(failed && capture)free(capture);
        assert(!live);
    }
    c->recent_count=0;c->wal.ready=false;c->wal.used=c->wal.bank_size=0;
    puts("Preparation sizing:8/16/full KiB, escape bound, canonical expansion, compaction fallback, joins and failed owners PASS");
}
static void reciprocal_cases(agent_engine_t *e,agent_context_t *c)
{
    calls=fail_at=0;ceiling=SIZE_MAX;
    void *out=(void *)1;
    assert(esp_agent_engine_memory_take_capture(NULL,1,&out)==AGENT_ERR_ARGUMENT);
    assert(esp_agent_engine_memory_take_capture(e,0,&out)==AGENT_ERR_ARGUMENT);
    assert(esp_agent_engine_memory_take_capture(e,1,NULL)==AGENT_ERR_ARGUMENT);
    assert(esp_agent_engine_memory_take_capture(e,1,&out)==AGENT_ERR_NOT_FOUND && out==(void *)1);
    static agent_engine_storage_t external;
    assert(!agent_engine_bind_parts(e,&external.work,sizeof(external.work),external.buffer,sizeof(external.buffer)));
    assert(!e->owned_state_bytes && esp_agent_engine_memory_take_capture(e,1,&out)==AGENT_ERR_NOT_FOUND);
    assert(!agent_engine_bind_parts(e,NULL,0,NULL,0));
    assert(!esp_agent_engine_memory_restore(e,true) && e->owned_state_bytes==AGENT_ENGINE_SCRATCH_SIZE);
    assert(esp_agent_engine_memory_take_capture(e,1,&out)==AGENT_ERR_NOT_FOUND && live==1);
    assert(!esp_agent_engine_memory_release(e) && !live && !e->owned_state_bytes);
    size_t size=sizeof(agent_engine_workspace_t),capacity=size+128;
    assert(!esp_agent_engine_memory_restore(e,false) && e->owned_state_bytes==size);
    unsigned prior=calls;char *request=e->buffer;void *state=e->scratch;
    assert(esp_agent_engine_memory_take_capture(e,capacity,&out)==AGENT_ERR_NOT_FOUND && calls==prior);
    assert(e->scratch==state && e->buffer==request && live==2);
    assert(!esp_agent_engine_memory_release(e) && !live);
    calls=0;
    unsigned char *capture=calloc(1,capacity);assert(capture);
    for(unsigned i=0;i<32;++i) {
        memset(capture,0xa5,capacity);
        assert(!esp_agent_engine_memory_adopt(e,capture,capacity));
        assert(e->owned_state_bytes==capacity && e->scratch_capacity==size && live==2);
        assert(capture[0]==0 && capture[size]==0xa5 && capture[capacity-1]==0xa5);
        state=e->workspace;request=e->buffer;prior=calls;out=(void *)1;
        assert(esp_agent_engine_memory_take_capture(e,capacity+1,&out)==AGENT_ERR_NOT_FOUND);
        for(unsigned active=0;active<3;++active) {
            c->prompt_locked=active==0;e->answer_active=active==1;e->progress_active=active==2;
            assert(esp_agent_engine_memory_take_capture(e,capacity,&out)==AGENT_ERR_BUSY);
            assert(calls==prior && live==2 && e->workspace==state && e->buffer==request && out==(void *)1);
        }
        c->prompt_locked=e->answer_active=e->progress_active=false;
        assert(!esp_agent_engine_memory_take_capture(e,capacity,&out));
        assert(out==capture && live==1 && calls==prior && !e->workspace && !e->buffer && !e->scratch);
        assert(!e->owned_state_bytes && !c->scratch && !c->capacity);
        /* Transfer itself neither zeroes nor consumes the old memory. Capture
         * owner resets its full logical region before handing out new views. */
        assert(capture[size]==0xa5 && capture[capacity-1]==0xa5);
        memset(capture,0,capacity);
        assert(e->failures==3 && e->circuit_until==987);
    }
    assert(calls==33); /* One original arena and32 separate request buffers. */
    assert(!esp_agent_engine_memory_release(e) && live==1); /* Caller still owns capture. */
    free(capture);assert(!live);
    puts("Reciprocal capture ownership:32 transfers, no state reallocation, unknown/whole/small/active rejection, bytes and config PASS");
}
#endif
int main(void)
{
    agent_context_t context={0};agent_engine_t e={.context=&context,.failures=3,.circuit_until=987};
    assert(AGENT_ENGINE_SCRATCH_SIZE>ceiling);
    assert(esp_agent_engine_memory_restore(&e,true)==AGENT_ERR_MEMORY && !live && !e.workspace);
    for(unsigned i=0;i<32;++i) {
        calls=0;fail_at=0;
        assert(!esp_agent_engine_memory_restore(&e,false) && live==2 && largest<=32768);
        assert(context.scratch==e.buffer && context.capacity==AGENT_ENGINE_BUFFER_SIZE);
        assert(e.workspace->reply.text[0]==0 && e.buffer[AGENT_REQUEST_MAX]==0);
        void *work=e.workspace,*buffer=e.buffer;unsigned allocated=calls;
        assert(!esp_agent_engine_memory_restore(&e,false) && calls==allocated);
        context.prompt_locked=true;
        assert(esp_agent_engine_memory_release(&e)==AGENT_ERR_BUSY);
        assert(esp_agent_engine_memory_restore(&e,true)==AGENT_ERR_BUSY);
        assert(e.workspace==work && e.buffer==buffer && live==2);
        context.prompt_locked=false;e.answer_active=true;
        assert(esp_agent_engine_memory_release(&e)==AGENT_ERR_BUSY);e.answer_active=false;
        assert(!esp_agent_engine_memory_release(&e) && !live && !e.buffer && !context.scratch);
        assert(e.failures==3 && e.circuit_until==987);
    }
    for(unsigned failure=1;failure<=2;++failure) {
        calls=0;fail_at=failure;
        assert(esp_agent_engine_memory_restore(&e,false)==AGENT_ERR_MEMORY);
        assert(!live && !e.workspace && !e.buffer && !e.scratch && !context.scratch);
        assert(agent_engine_turn(&e,"valid input",true)==AGENT_ERR_MEMORY);
    }
    calls=0;fail_at=0;ceiling=SIZE_MAX;
    assert(!esp_agent_engine_memory_restore(&e,false) && live==2);
    assert(!esp_agent_engine_memory_restore(&e,true) && live==1);
    assert(e.scratch_capacity==AGENT_ENGINE_SCRATCH_SIZE && context.scratch==e.buffer);
    /* Existing whole arenas remain reusable without churn until capture ends. */
    unsigned allocated=calls;
    assert(!esp_agent_engine_memory_restore(&e,false) && calls==allocated && live==1);
    assert(!esp_agent_engine_memory_release(&e) && !live);
#if AGENT_REQUEST_SCRATCH_COMPACT
    compact_cases(&e,&context);
    reciprocal_cases(&e,&context);
#endif
    assert(!esp_agent_engine_memory_release(&e));
    /* Capture arena remains elsewhere: request-only stage has one allocation.
     * Full-state restore must retain its bytes/address, including failures. */
    for(unsigned failure=0;failure<2;++failure) {
        calls=0;fail_at=0;
        assert(!esp_agent_engine_memory_prepare(&e) && live==1 && !e.workspace);
        char *request=e.buffer;request[6151]=73;
        assert(!esp_agent_engine_memory_prepare(&e) && calls==1);
        context.prompt_locked=true;
        assert(esp_agent_engine_memory_restore(&e,false)==AGENT_ERR_BUSY);
        assert(live==1 && e.buffer==request && !e.workspace);
        context.prompt_locked=false;
        fail_at=failure?calls+1:0;
        agent_err_t result=esp_agent_engine_memory_restore(&e,false);
        assert(result==(failure?AGENT_ERR_MEMORY:AGENT_OK));
        assert(e.buffer==request && request[6151]==73 && context.scratch==request);
        assert(live==(failure?1u:2u));
        assert(!esp_agent_engine_memory_release(&e) && !live && !context.scratch);
    }
    calls=0;fail_at=1;
    assert(esp_agent_engine_memory_prepare(&e)==AGENT_ERR_MEMORY && !e.buffer && !live);
    for(unsigned prepared=0;prepared<2;++prepared) {
        fail_at=0;ceiling=SIZE_MAX;
        size_t size=sizeof(agent_engine_workspace_t);
        unsigned char *capture=calloc(1,size+64);assert(capture);
        memset(capture,0xa5,size+64);
        if(prepared)assert(!esp_agent_engine_memory_prepare(&e));
        char *request=e.buffer;
        if(request)request[6151]=73;
        unsigned prior_calls=calls;
        assert(esp_agent_engine_memory_adopt(&e,capture,size-1)==AGENT_ERR_ARGUMENT);
        assert(esp_agent_engine_memory_adopt(&e,capture+1,size)==AGENT_ERR_ARGUMENT);
        context.prompt_locked=true;
        assert(esp_agent_engine_memory_adopt(&e,capture,size)==AGENT_ERR_BUSY);
        context.prompt_locked=false;e.answer_active=true;
        assert(esp_agent_engine_memory_adopt(&e,capture,size)==AGENT_ERR_BUSY);e.answer_active=false;
        assert(calls==prior_calls && !e.workspace && capture[0]==0xa5);
        /* No contiguous block as large as state is now available. With a
         * request already bound, prohibit ALL new allocations. */
        ceiling=prepared?0:AGENT_ENGINE_BUFFER_SIZE;
        assert(sizeof(agent_engine_workspace_t)>AGENT_ENGINE_BUFFER_SIZE);
        assert(!esp_agent_engine_memory_adopt(&e,capture,size+64));
        assert(calls==prior_calls+(prepared?0u:1u) && live==2);
        assert(e.workspace==(void *)capture && e.scratch==capture);
        for(size_t i=0;i<size;++i)assert(capture[i]==0);
        for(size_t i=size;i<size+64;++i)assert(capture[i]==0xa5);
        if(request)assert(e.buffer==request && request[6151]==73);
        assert(context.scratch==e.buffer && context.capacity==AGENT_ENGINE_BUFFER_SIZE);
        assert(!esp_agent_engine_memory_release(&e) && !live && !e.buffer);
    }
    ceiling=SIZE_MAX;fail_at=0;
    unsigned char *capture=calloc(1,sizeof(agent_engine_workspace_t));assert(capture);capture[0]=123;
    fail_at=calls+1;
    assert(esp_agent_engine_memory_adopt(&e,capture,sizeof(agent_engine_workspace_t))==AGENT_ERR_MEMORY);
    assert(live==1 && !e.workspace && !e.buffer && capture[0]==123);
    assert(!esp_agent_engine_memory_release(&e) && live==1); /* Still caller-owned. */
    fail_at=0;
    assert(!esp_agent_engine_memory_adopt(&e,capture,sizeof(agent_engine_workspace_t)));
    assert(!esp_agent_engine_memory_release(&e) && !live);
    puts("Capture adoption: same arena, no new state allocation, canaries, lock/failure ownership and request address PASS");
    puts("Engine memory:45KiB fragmentation, partial failures, live readers and full/split lifetime OK");
}
