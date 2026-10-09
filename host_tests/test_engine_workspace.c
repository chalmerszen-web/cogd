#include "engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint64_t before; agent_engine_storage_t data; uint64_t after; } guarded_t;
static guarded_t first,second;
static agent_context_t context;
static agent_engine_t engine;

int main(void)
{
    first.before=second.before=0x123456789abcdef0ull;
    first.after=second.after=0xfedcba9876543210ull;
    engine.context=&context;engine.failures=2;engine.circuit_until=987654;
    strcpy(engine.turn_id,"retained-turn");context.cursor=73;context.acked=61;
    assert(!agent_engine_bind_workspace(&engine,&first.data,sizeof(first.data)));
    size_t bytes=0;
    assert(agent_engine_scratch(&engine,&bytes)==&first.data && bytes==sizeof(first.data));
    assert(context.scratch==first.data.buffer && context.capacity==sizeof(first.data.buffer));
    assert(agent_engine_bind_workspace(NULL,&second.data,sizeof(second.data))==AGENT_ERR_ARGUMENT);
    assert(agent_engine_bind_workspace(&engine,NULL,1)==AGENT_ERR_ARGUMENT);
    assert(agent_engine_bind_workspace(&engine,&second.data,sizeof(second.data)-1)==AGENT_ERR_ARGUMENT);
    assert(agent_engine_bind_workspace(&engine,(char *)&second.data+1,sizeof(second.data))==AGENT_ERR_ARGUMENT);
    assert(engine.workspace==&first.data.work && context.scratch==first.data.buffer);
    for(unsigned active=0;active<2;++active) {
        engine.answer_active=active==0;engine.progress_active=active==1;
        assert(agent_engine_bind_workspace(&engine,NULL,0)==AGENT_ERR_BUSY);
        assert(agent_engine_bind_workspace(&engine,&second.data,sizeof(second.data))==AGENT_ERR_BUSY);
        assert(engine.workspace==&first.data.work && context.scratch==first.data.buffer);
    }
    engine.answer_active=engine.progress_active=false;
    context.prompt_locked=true;
    assert(agent_engine_bind_workspace(&engine,NULL,0)==AGENT_ERR_BUSY);
    assert(engine.workspace==&first.data.work && context.scratch==first.data.buffer);
    context.prompt_locked=false;
    for(unsigned i=0;i<32;++i) {
        assert(!agent_engine_bind_workspace(&engine,NULL,0));
        assert(!agent_engine_scratch(&engine,&bytes) && !bytes);
        assert(!context.scratch && !context.capacity);
        /* No runtime/configuration exists: detached storage must fail before
         * attempting network, WAL or tools, including after failed allocation. */
        assert(agent_engine_turn(&engine,"valid input",true)==AGENT_ERR_MEMORY);
        guarded_t *next=i%2?&first:&second;
        assert(!agent_engine_bind_workspace(&engine,&next->data,sizeof(next->data)));
        memset(agent_engine_scratch(&engine,&bytes),0xa5,sizeof(next->data));
        assert(context.scratch==next->data.buffer && context.capacity==AGENT_REQUEST_MAX+1);
        assert(engine.failures==2 && engine.circuit_until==987654 && !strcmp(engine.turn_id,"retained-turn"));
        assert(context.cursor==73 && context.acked==61);
        assert(first.before==0x123456789abcdef0ull && second.before==first.before);
        assert(first.after==0xfedcba9876543210ull && second.after==first.after);
    }
    assert(!agent_engine_bind_parts(&engine,&first.data.work,sizeof(first.data.work),second.data.buffer,sizeof(second.data.buffer)));
    assert(engine.workspace==&first.data.work && engine.buffer==second.data.buffer && context.scratch==second.data.buffer);
    assert(agent_engine_scratch(&engine,&bytes)==&first.data.work && bytes==sizeof(first.data.work));
    assert(agent_engine_bind_parts(&engine,&first.data.work,sizeof(first.data.work),&first.data.work,sizeof(second.data.buffer))==AGENT_ERR_ARGUMENT);
    assert(agent_engine_bind_parts(&engine,&first.data.work,sizeof(first.data.work),second.data.buffer+1,sizeof(second.data.buffer))==AGENT_ERR_ARGUMENT);
    assert(agent_engine_bind_parts(&engine,&first.data.work,sizeof(first.data.work),second.data.buffer,sizeof(second.data.buffer)-1)==AGENT_ERR_ARGUMENT);
    assert(agent_engine_bind_parts(&engine,NULL,0,second.data.buffer,sizeof(second.data.buffer))==AGENT_ERR_ARGUMENT);
    assert(agent_engine_bind_parts(&engine,&first.data.work,sizeof(first.data.work),NULL,0)==AGENT_ERR_ARGUMENT);
    assert(engine.workspace==&first.data.work && engine.buffer==second.data.buffer);
    context.prompt_locked=true;
    assert(agent_engine_bind_parts(&engine,&second.data.work,sizeof(second.data.work),first.data.buffer,sizeof(first.data.buffer))==AGENT_ERR_BUSY);
    context.prompt_locked=false;
    assert(!agent_engine_bind_workspace(&engine,NULL,0) && !engine.buffer && !engine.scratch);
    assert(agent_engine_bind_buffer(&engine,first.data.buffer+1,sizeof(first.data.buffer))==AGENT_ERR_ARGUMENT);
    assert(!agent_engine_bind_buffer(&engine,first.data.buffer,sizeof(first.data.buffer)));
    assert(!engine.workspace && !engine.scratch && !engine.scratch_capacity && context.scratch==engine.buffer);
    assert(agent_engine_turn(&engine,"ordinary requires state",true)==AGENT_ERR_MEMORY);
    context.prompt_locked=true;
    assert(agent_engine_bind_buffer(&engine,second.data.buffer,sizeof(second.data.buffer))==AGENT_ERR_BUSY);
    context.prompt_locked=false;
    assert(!agent_engine_bind_parts(&engine,&first.data.work,sizeof(first.data.work),first.data.buffer,sizeof(first.data.buffer)));
    assert(agent_engine_bind_buffer(&engine,second.data.buffer,sizeof(second.data.buffer))==AGENT_ERR_BUSY);
    assert(!agent_engine_bind_workspace(&engine,NULL,0));
#if AGENT_REQUEST_SCRATCH_COMPACT
    static _Alignas(8) char preparation[8192];
    assert(agent_engine_bind_preparation_buffer(NULL,preparation,sizeof(preparation))==AGENT_ERR_ARGUMENT);
    assert(agent_engine_bind_preparation_buffer(&engine,NULL,sizeof(preparation))==AGENT_ERR_ARGUMENT);
    assert(agent_engine_bind_preparation_buffer(&engine,preparation+1,sizeof(preparation))==AGENT_ERR_ARGUMENT);
    assert(agent_engine_bind_preparation_buffer(&engine,preparation,sizeof(preparation)-1)==AGENT_ERR_ARGUMENT);
    assert(agent_engine_bind_preparation_buffer(&engine,(void *)(UINTPTR_MAX-3),sizeof(preparation))==AGENT_ERR_ARGUMENT);
    assert(!agent_engine_bind_preparation_buffer(&engine,preparation,sizeof(preparation)));
    assert(engine.preparation_capacity==8192 && context.capacity==8192 && !engine.workspace);
    assert(agent_engine_turn(&engine,"ordinary requires full state",true)==AGENT_ERR_MEMORY);
    assert(agent_engine_bind_parts(&engine,&first.data.work,sizeof(first.data.work),preparation,sizeof(preparation))==AGENT_ERR_ARGUMENT);
    context.prompt_locked=true;
    assert(agent_engine_bind_preparation_buffer(&engine,preparation,sizeof(preparation))==AGENT_ERR_BUSY);
    context.prompt_locked=false;engine.progress_active=true;
    assert(agent_engine_bind_preparation_buffer(&engine,preparation,sizeof(preparation))==AGENT_ERR_BUSY);
    engine.progress_active=false;
    assert(!agent_engine_bind_buffer(&engine,first.data.buffer,sizeof(first.data.buffer)));
    assert(!engine.preparation_capacity && context.capacity==sizeof(first.data.buffer));
    assert(!agent_engine_bind_workspace(&engine,NULL,0));
#endif
    puts("Engine workspace: split/full binding, overlap/size rejection, active-reader refusal and retained state OK");
}
