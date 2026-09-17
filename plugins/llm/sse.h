#ifndef AGENT_SSE_H
#define AGENT_SSE_H
#include "llm.h"

typedef struct {
    char *frame;
    size_t capacity;
    size_t used, line_start;
    bool skip_lf;
    agent_llm_reply_t *reply;
    agent_emit_fn emit;
    void *ctx;
    agent_err_t error;
} agent_sse_t;

/* Borrows frame until finish; initialization never writes its contents. */
void agent_sse_init(agent_sse_t *sse, char *frame, size_t capacity, agent_llm_reply_t *reply, agent_emit_fn emit, void *ctx);
agent_err_t agent_sse_feed(void *ctx, const char *bytes, size_t length);
agent_err_t agent_sse_finish(const agent_sse_t *sse);
#endif
