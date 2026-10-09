#ifndef AGENT_TRANSPORT_H
#define AGENT_TRANSPORT_H
#include "agent.h"

typedef enum { AGENT_HTTP_DEEPSEEK, AGENT_HTTP_GATEWAY, AGENT_HTTP_VOCALIGN, AGENT_HTTP_PUBLIC } agent_http_target_t;
/* Optional synchronous diagnostics. Must not mutate request state or borrow
 * producer/response scratch. Implementations may omit transport phases. */
typedef void (*agent_http_trace_fn)(void *ctx,const char *phase);
typedef struct {
    agent_http_target_t target;
    const char *path;
    const char *method;
    const char *body;
    size_t length;
    const atomic_bool *cancelled;
    agent_body_fn produce;
    void *body_ctx;
    const char *content_type; /* NULL = JSON for POST, omitted for GET. PUBLIC never receives credentials. */
    bool reuse_connection; /* Same authenticated origin, within one worker job only. */
    /* Optional synchronous handoff after the complete body is sent, before
     * reading the response. The body/producer must not be accessed afterward.
     * A failure aborts the request. Older adapters may omit this callback. */
    agent_err_t (*on_sent)(void *ctx);
    void *sent_ctx;
    agent_http_trace_fn trace;
    void *trace_ctx;
} agent_http_request_t;
typedef agent_err_t (*agent_http_feed_fn)(void *ctx, const char *data, size_t length);
typedef struct {
    /* Body is no longer accessed after the first feed callback: safe buffer reuse. */
    agent_err_t (*perform)(void *ctx, const agent_http_request_t *request, agent_http_feed_fn feed, void *feed_ctx);
    void *ctx;
} agent_transport_ops_t;

/* Synchronous, bounded chunks; validates that the producer writes exactly length. */
agent_err_t agent_http_write_body(const agent_http_request_t *, agent_write_fn, void *);
/* Coalesce tiny JSON fragments; large slices pass synchronously without a copy. */
agent_err_t agent_http_write_buffered(const agent_http_request_t *,agent_write_fn,void *,char *,size_t);
agent_err_t agent_http_measure_body(agent_body_fn, void *, const atomic_bool *, size_t *);
#endif
