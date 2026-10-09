#include "engine_memory.h"
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(agent_engine_workspace_t)<=32768,"State allocation must stay below32KiB");
_Static_assert(AGENT_ENGINE_BUFFER_SIZE<=32768,"Request allocation must stay below32KiB");

#if AGENT_REQUEST_SCRATCH_COMPACT
/* The caller has joined the capture/candidate owner. No sink or selected
 * history can still borrow these bytes. Free before allocating full scratch
 * so both request buffers are never live together. */
static agent_err_t full_buffer(agent_engine_t *e)
{
    if(!e->preparation_capacity)return AGENT_OK;
    void *old=e->buffer;
    agent_err_t error=agent_engine_bind_parts(e,NULL,0,NULL,0);
    if(error)return error;
    free(old);
    void *buffer=calloc(1,AGENT_ENGINE_BUFFER_SIZE);
    if(!buffer)return AGENT_ERR_MEMORY;
    error=agent_engine_bind_buffer(e,buffer,AGENT_ENGINE_BUFFER_SIZE);
    if(error)free(buffer);
    return error;
}
#endif

agent_err_t esp_agent_engine_memory_release(agent_engine_t *e)
{
    void *memory=e->scratch,*buffer=e->buffer;
    bool split=!memory || e->scratch_capacity==sizeof(agent_engine_workspace_t);
    agent_err_t error=agent_engine_bind_workspace(e,NULL,0);
    if(error)return error;
    free(memory);
    if(split)free(buffer);
    return AGENT_OK;
}

agent_err_t esp_agent_engine_memory_restore(agent_engine_t *e,bool contiguous)
{
    if(e->workspace && (!contiguous || e->scratch_capacity==AGENT_ENGINE_SCRATCH_SIZE))return AGENT_OK;
    if(!contiguous && !e->workspace && e->buffer) {
#if AGENT_REQUEST_SCRATCH_COMPACT
        agent_err_t expanded=full_buffer(e);
        if(expanded)return expanded;
#endif
        void *state=calloc(1,sizeof(agent_engine_workspace_t));
        if(!state)return AGENT_ERR_MEMORY;
        agent_err_t error=agent_engine_bind_parts(e,state,sizeof(agent_engine_workspace_t),
                                                e->buffer,AGENT_ENGINE_BUFFER_SIZE);
        if(error)free(state);
#if AGENT_REQUEST_SCRATCH_COMPACT
        else e->owned_state_bytes=sizeof(agent_engine_workspace_t);
#endif
        return error;
    }
    agent_err_t error=esp_agent_engine_memory_release(e);
    if(error)return error;
    size_t size=contiguous?AGENT_ENGINE_SCRATCH_SIZE:sizeof(agent_engine_workspace_t);
    void *memory=calloc(1,size);
    if(!memory)return AGENT_ERR_MEMORY;
    if(contiguous) {
        error=agent_engine_bind_workspace(e,memory,size);
    } else {
        void *buffer=calloc(1,AGENT_ENGINE_BUFFER_SIZE);
        error=buffer?agent_engine_bind_parts(e,memory,size,buffer,AGENT_ENGINE_BUFFER_SIZE):AGENT_ERR_MEMORY;
        if(error)free(buffer);
    }
    if(error)free(memory);
#if AGENT_REQUEST_SCRATCH_COMPACT
    else e->owned_state_bytes=size;
#endif
    return error;
}

agent_err_t esp_agent_engine_memory_prepare(agent_engine_t *e)
{
    if(e->workspace || e->scratch)return AGENT_ERR_BUSY;
#if AGENT_REQUEST_SCRATCH_COMPACT
    if(e->preparation_capacity)return AGENT_ERR_BUSY;
#endif
    if(e->buffer)return AGENT_OK;
    void *buffer=calloc(1,AGENT_ENGINE_BUFFER_SIZE);
    if(!buffer)return AGENT_ERR_MEMORY;
    agent_err_t error=agent_engine_bind_buffer(e,buffer,AGENT_ENGINE_BUFFER_SIZE);
    if(error)free(buffer);
    return error;
}

#if AGENT_REQUEST_SCRATCH_COMPACT
static size_t preparation_size(const agent_context_t *c,size_t input_bytes)
{
    if(!c || !c->wal.ready || c->wal.tail_recovered || c->recent_count>AGENT_RECENT_MAX ||
       c->wal.used>c->wal.bank_size || c->wal.bank_size-c->wal.used<AGENT_REQUEST_MAX+32u)
        return AGENT_ENGINE_BUFFER_SIZE;
    /* Each input byte can become six JSON bytes. The remaining2049 covers
     * both64-byte IDs fully escaped, device/event IDs, numeric fields, static
     * envelope and cJSON's print slack. Public input limit remains2KiB. */
    size_t required=2049u+6u*input_bytes;
    for(unsigned i=0;i<c->recent_count;++i) {
        if(!c->recent_message_bytes[i] || c->recent[i].length>=AGENT_ENGINE_BUFFER_SIZE)
            return AGENT_ENGINE_BUFFER_SIZE;
        size_t raw=(size_t)c->recent[i].length+1u,canonical=(size_t)c->recent_message_bytes[i]+5u;
        if(raw>required)required=raw;
        if(canonical>required)required=canonical;
    }
    return required<=AGENT_ENGINE_PREPARATION_MIN?AGENT_ENGINE_PREPARATION_MIN:
           required<=16384u?16384u:AGENT_ENGINE_BUFFER_SIZE;
}

agent_err_t esp_agent_engine_memory_prepare_input(agent_engine_t *e,const char *input)
{
    if(!e || !input)return AGENT_ERR_ARGUMENT;
    size_t bytes=strlen(input);
    if(!bytes || bytes>AGENT_INPUT_MAX || !agent_utf8_valid(input,bytes))return AGENT_ERR_LIMIT;
    if(e->workspace || e->scratch || e->answer_active || e->progress_active ||
       (e->context && e->context->prompt_locked))return AGENT_ERR_BUSY;
    size_t capacity=preparation_size(e->context,bytes);
    if(e->buffer)return !e->preparation_capacity || e->preparation_capacity>=capacity?AGENT_OK:AGENT_ERR_BUSY;
    void *buffer=calloc(1,capacity);
    if(!buffer)return AGENT_ERR_MEMORY;
    agent_err_t error=agent_engine_bind_preparation_buffer(e,buffer,capacity);
    if(error)free(buffer);
    return error;
}

agent_err_t esp_agent_engine_memory_take_capture(agent_engine_t *e,size_t minimum,void **out)
{
    if(!e || !out || !minimum)return AGENT_ERR_ARGUMENT;
    if(!e->workspace || !e->scratch || e->scratch_capacity!=sizeof(agent_engine_workspace_t) ||
       e->owned_state_bytes<minimum)return AGENT_ERR_NOT_FOUND;
    void *memory=e->scratch,*buffer=e->buffer;
    agent_err_t error=agent_engine_bind_parts(e,NULL,0,NULL,0);
    if(error)return error;
    free(buffer);*out=memory;
    return AGENT_OK;
}
#endif

agent_err_t esp_agent_engine_memory_adopt(agent_engine_t *e,void *memory,size_t capacity)
{
    if(!e || !memory || capacity<sizeof(agent_engine_workspace_t) ||
       (uintptr_t)memory%_Alignof(agent_engine_workspace_t))return AGENT_ERR_ARGUMENT;
    if(e->workspace || e->scratch || e->answer_active || e->progress_active ||
       (e->context && e->context->prompt_locked))return AGENT_ERR_BUSY;
#if AGENT_REQUEST_SCRATCH_COMPACT
    if(e->preparation_capacity) {
        uintptr_t a=(uintptr_t)memory,b=(uintptr_t)e->buffer;
        if(a>UINTPTR_MAX-sizeof(agent_engine_workspace_t) ||
           (a<b+e->preparation_capacity && b<a+sizeof(agent_engine_workspace_t)))return AGENT_ERR_ARGUMENT;
        agent_err_t expanded=full_buffer(e);
        if(expanded)return expanded;
    }
#endif
    bool allocated=!e->buffer;
    void *buffer=allocated?calloc(1,AGENT_ENGINE_BUFFER_SIZE):e->buffer;
    if(!buffer)return AGENT_ERR_MEMORY;
    agent_err_t error=agent_engine_bind_parts(e,memory,capacity,buffer,AGENT_ENGINE_BUFFER_SIZE);
    if(error) {if(allocated)free(buffer);}
    else {
        memset(memory,0,sizeof(agent_engine_workspace_t));
#if AGENT_REQUEST_SCRATCH_COMPACT
        e->owned_state_bytes=capacity;
#endif
    }
    return error;
}
