#ifndef AGENT_ENGINE_H
#define AGENT_ENGINE_H
#include "context.h"
#include "sse.h"
#include "tools.h"
#include "transport.h"

typedef struct {
    agent_err_t (*build)(const agent_messages_t *,bool,const char *,char *,size_t,size_t *);
    agent_err_t (*parse)(agent_llm_reply_t *,const char *,size_t,bool,agent_emit_fn,void *);
    agent_err_t (*compose)(bool,agent_body_fn,void *,agent_body_fn,void *,agent_write_fn,void *);
} agent_llm_ops_t;
typedef struct {
    agent_core_t *core;
    const agent_transport_ops_t *transport;
    const agent_llm_ops_t *llm;
    const agent_tool_ops_t *tools;
    agent_context_t *context;
    const agent_context_ops_t *context_ops;
    agent_emit_fn emit;
    void *emit_ctx;
    void (*pause_ms)(unsigned);
    uint32_t (*random_u32)(void);
    bool gateway,output_started,effects;
    unsigned failures;
    uint64_t circuit_until;
    char turn_id[64];
    agent_messages_t messages;
    agent_llm_reply_t reply;
    agent_sse_t sse;
    char buffer[AGENT_REQUEST_MAX+1];
    size_t used,total_text;
    uint64_t history_before;
} agent_engine_t;
/* Borrow only while the caller owns the turn reservation and work lock.
 * Messages, reply and SSE are initialized afresh by every subsequent turn.
 * Configuration, context indexes and circuit state remain outside this range. */
#define AGENT_ENGINE_SCRATCH_SIZE (offsetof(agent_engine_t,used)-offsetof(agent_engine_t,messages))
static inline void *agent_engine_scratch(agent_engine_t *engine,size_t *capacity)
{
    *capacity=AGENT_ENGINE_SCRATCH_SIZE;
    return (unsigned char *)engine+offsetof(agent_engine_t,messages);
}
agent_err_t agent_engine_turn(agent_engine_t *,const char *input,bool stream);
#endif
