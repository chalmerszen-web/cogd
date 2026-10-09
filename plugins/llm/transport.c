#include "transport.h"
#include <string.h>

typedef struct {
    agent_write_fn write;
    void *ctx;
    const atomic_bool *cancelled;
    size_t used, limit;
    agent_err_t error;
} body_writer_t;

static agent_err_t part(void *ctx, const char *data, size_t length)
{
    body_writer_t *w = ctx;
    if(w->error) return w->error;
    if ((!data && length) || length > w->limit - w->used) return w->error=AGENT_ERR_LIMIT;
    while (length) {
        if (w->cancelled && atomic_load(w->cancelled)) return w->error=AGENT_ERR_CANCELLED;
        size_t n = length > 4096 ? 4096 : length;
        agent_err_t error = w->write ? w->write(w->ctx, data, n) : AGENT_OK;
        if (error) return w->error=error;
        w->used += n; data += n; length -= n;
    }
    return AGENT_OK;
}

agent_err_t agent_http_measure_body(agent_body_fn produce, void *ctx, const atomic_bool *cancelled, size_t *length)
{
    if (!produce || !length) return AGENT_ERR_ARGUMENT;
    if (cancelled && atomic_load(cancelled)) return AGENT_ERR_CANCELLED;
    body_writer_t w = {.cancelled = cancelled, .limit = AGENT_HTTP_REQUEST_MAX};
    agent_err_t error = produce(ctx, part, &w);
    *length = w.used;
    return error ? error : w.error;
}

agent_err_t agent_http_write_body(const agent_http_request_t *request, agent_write_fn write, void *ctx)
{
    if (!request || !write || request->length > AGENT_HTTP_REQUEST_MAX ||
        (request->body && request->produce) || (!request->body && !request->produce && request->length))
        return AGENT_ERR_ARGUMENT;
    body_writer_t w = {.write=write,.ctx=ctx,.cancelled=request->cancelled,.limit=request->length};
    if (w.cancelled && atomic_load(w.cancelled)) return AGENT_ERR_CANCELLED;
    agent_err_t error = request->produce ? request->produce(request->body_ctx, part, &w) :
        part(&w, request->body, request->length);
    return error ? error : w.error ? w.error : w.used == request->length ? AGENT_OK : AGENT_ERR_PROTOCOL;
}

typedef struct { agent_write_fn write; void *ctx; char *data; size_t used,capacity; } buffered_t;
static agent_err_t buffered_part(void *ctx,const char *data,size_t length)
{
    buffered_t *b=ctx;
    while(length) {
        /* Large producer slices already have stable storage for this
         * synchronous write. Keep their <=4096-byte framing instead of
         * copying and splitting them into extra small TLS records. */
        if(!b->used && length>=b->capacity) return b->write(b->ctx,data,length);
        size_t n=b->capacity-b->used;if(n>length) n=length;
        memcpy(b->data+b->used,data,n);b->used+=n;data+=n;length-=n;
        if(b->used==b->capacity) {
            agent_err_t e=b->write(b->ctx,b->data,b->used);if(e) return e;
            b->used=0;
        }
    }
    return AGENT_OK;
}
agent_err_t agent_http_write_buffered(const agent_http_request_t *r,agent_write_fn write,void *ctx,char *scratch,size_t capacity)
{
    if(!write || !scratch || !capacity) return AGENT_ERR_ARGUMENT;
    buffered_t b={.write=write,.ctx=ctx,.data=scratch,.capacity=capacity};
    agent_err_t e=agent_http_write_body(r,buffered_part,&b);
    if(!e && r->cancelled && atomic_load(r->cancelled)) e=AGENT_ERR_CANCELLED;
    return e?e:b.used?write(ctx,scratch,b.used):AGENT_OK;
}
