#include "sse.h"
#include <string.h>

void agent_sse_init(agent_sse_t *sse, char *frame, size_t capacity, agent_llm_reply_t *reply, agent_emit_fn emit, void *ctx)
{
    memset(sse, 0, sizeof(*sse));
    sse->frame = frame; sse->capacity = capacity;
    if (!frame || capacity < AGENT_STREAM_MAX + 1) sse->error = AGENT_ERR_LIMIT;
    sse->reply = reply; sse->emit = emit; sse->ctx = ctx;
}

static agent_err_t dispatch(agent_sse_t *sse)
{
    size_t src = 0, dst = 0;
    bool has_data = false;
    while (src < sse->used) {
        size_t end = src;
        while (end < sse->used && sse->frame[end] != '\n') ++end;
        size_t start = src;
        if (end - start >= 5 && !memcmp(sse->frame + start, "data:", 5)) {
            start += 5;
            if (start < end && sse->frame[start] == ' ') ++start;
            if (has_data) sse->frame[dst++] = '\n';
            memmove(sse->frame + dst, sse->frame + start, end - start);
            dst += end - start; has_data = true;
        } else if (end - start == 4 && !memcmp(sse->frame + start, "data", 4)) {
            if (has_data) sse->frame[dst++] = '\n';
            has_data = true;
        }
        src = end + 1;
    }
    sse->frame[dst] = 0;
    if (!has_data || !dst) return AGENT_OK;
    if (sse->reply->done) return AGENT_ERR_PROTOCOL;
    if (dst == 6 && !memcmp(sse->frame, "[DONE]", 6)) {
        if (!sse->reply->finished) return AGENT_ERR_PROTOCOL;
        sse->reply->done = true;
        const agent_event_t event = { .type = AGENT_EVENT_DONE };
        return sse->emit ? sse->emit(sse->ctx, &event) : AGENT_OK;
    }
    return agent_llm_parse(sse->reply, sse->frame, dst, true, sse->emit, sse->ctx);
}

static agent_err_t newline(agent_sse_t *sse)
{
    if (sse->line_start == sse->used) {
        agent_err_t error = dispatch(sse);
        sse->used = sse->line_start = 0;
        return error;
    }
    if (sse->used >= AGENT_STREAM_MAX) return AGENT_ERR_LIMIT;
    sse->frame[sse->used++] = '\n';
    sse->line_start = sse->used;
    return AGENT_OK;
}

agent_err_t agent_sse_feed(void *ctx, const char *bytes, size_t length)
{
    agent_sse_t *sse = ctx;
    if (sse->error) return sse->error;
    for (size_t i = 0; i < length; ++i) {
        char ch = bytes[i];
        if (sse->skip_lf) { sse->skip_lf = false; if (ch == '\n') continue; }
        if (ch == '\r' || ch == '\n') {
            sse->skip_lf = ch == '\r';
            sse->error = newline(sse);
        } else if (!ch) sse->error = AGENT_ERR_PROTOCOL;
        else if (sse->used >= AGENT_STREAM_MAX) sse->error = AGENT_ERR_LIMIT;
        else sse->frame[sse->used++] = ch;
        if (sse->error) return sse->error;
    }
    return AGENT_OK;
}

agent_err_t agent_sse_finish(const agent_sse_t *sse)
{
    if (sse->error) return sse->error;
    return sse->reply->done && sse->reply->finished && !sse->used ? AGENT_OK : AGENT_ERR_PROTOCOL;
}
