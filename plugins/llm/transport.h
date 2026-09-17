#ifndef AGENT_TRANSPORT_H
#define AGENT_TRANSPORT_H
#include "agent.h"

typedef enum { AGENT_HTTP_DEEPSEEK, AGENT_HTTP_GATEWAY } agent_http_target_t;
typedef struct {
    agent_http_target_t target;
    const char *path;
    const char *method;
    const char *body;
    size_t length;
    const atomic_bool *cancelled;
    agent_body_fn produce;
    void *body_ctx;
} agent_http_request_t;
typedef agent_err_t (*agent_http_feed_fn)(void *ctx, const char *data, size_t length);
typedef struct {
    /* Body is no longer accessed after the first feed callback: safe buffer reuse. */
    agent_err_t (*perform)(void *ctx, const agent_http_request_t *request, agent_http_feed_fn feed, void *feed_ctx);
    void *ctx;
} agent_transport_ops_t;

/* Synchronous, bounded chunks; validates that the producer writes exactly length. */
agent_err_t agent_http_write_body(const agent_http_request_t *, agent_write_fn, void *);
agent_err_t agent_http_measure_body(agent_body_fn, void *, const atomic_bool *, size_t *);
#endif
