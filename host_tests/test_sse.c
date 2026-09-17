#include "sse.h"
#include "transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char stream[] =
    ": heartbeat\r\n\r\n"
    "data: {\"choices\":[{\"delta\":{\"content\":\"你好🙂\"},\"finish_reason\":null}]}\r\n\r\n"
    "event: message\ndata: {\"choices\":\ndata: [{\"delta\":{\"tool_calls\":[{\"index\":1,\"id\":\"call-b\",\"function\":{\"name\":\"device_status_get\",\"arguments\":\"{\"}}]}}]}\n\n"
    "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call-a\",\"function\":{\"name\":\"device_light_set_rgb\",\"arguments\":\"{\\\"r\\\":255,\"}}]}}]}\n\n"
    "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":1,\"function\":{\"arguments\":\"}\"}},{\"index\":0,\"function\":{\"arguments\":\"\\\"g\\\":0,\\\"b\\\":0}\"}}]}}]}\n\n"
    "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"tool_calls\"}]}\n\n"
    "data: [DONE]\n\n";

typedef struct { char text[128]; unsigned begin, end, done; } capture_t;
static agent_err_t capture(void *ctx, const agent_event_t *event)
{
    capture_t *out = ctx;
    if (event->type == AGENT_EVENT_TEXT) {
        assert(strlen(out->text) + event->length < sizeof(out->text)); strcat(out->text, event->data);
    }
    if (event->type == AGENT_EVENT_TOOL_BEGIN) ++out->begin;
    if (event->type == AGENT_EVENT_TOOL_END) ++out->end;
    if (event->type == AGENT_EVENT_DONE) ++out->done;
    return AGENT_OK;
}

static agent_err_t collect_request(void *ctx, const char *bytes, size_t length)
{
    size_t *used=ctx;
    assert(length==7 && !memcmp(bytes,"request",7));
    *used+=length;
    return AGENT_OK;
}

static void fragmented(size_t chunk, uint32_t seed)
{
    agent_llm_reply_t reply;
    agent_sse_t sse;
    char frame[AGENT_STREAM_MAX + 1];
    capture_t output = {0};
    agent_llm_reply_init(&reply); agent_sse_init(&sse, frame, sizeof(frame), &reply, capture, &output);
    for (size_t offset = 0; offset < sizeof(stream) - 1;) {
        seed = seed * 1664525u + 1013904223u;
        size_t n = chunk ? chunk : 1 + seed % 71;
        if (n > sizeof(stream) - 1 - offset) n = sizeof(stream) - 1 - offset;
        assert(agent_sse_feed(&sse, stream + offset, n) == AGENT_OK); offset += n;
    }
    assert(agent_sse_finish(&sse) == AGENT_OK);
    assert(!strcmp(output.text, "你好🙂") && output.begin == 2 && output.end == 2 && output.done == 1);
    assert(reply.call_count == 2 && !strcmp(reply.calls[0].id, "call-a"));
    assert(!strcmp(agent_call_arguments(&reply, 0), "{\"r\":255,\"g\":0,\"b\":0}"));
    assert(!strcmp(agent_call_arguments(&reply, 1), "{}"));
}

int main(void)
{
    size_t chunks[] = {1, 2, 3, 7, 31, sizeof(stream)};
    for (size_t i = 0; i < sizeof(chunks) / sizeof(chunks[0]); ++i) fragmented(chunks[i], 1);
    for (uint32_t seed = 0; seed < 200; ++seed) fragmented(0, seed);
    agent_llm_reply_t reply;
    agent_sse_t sse;
    char frame[AGENT_STREAM_MAX + 1];
    agent_llm_reply_init(&reply); agent_sse_init(&sse, frame, sizeof(frame), &reply, NULL, NULL);
    assert(agent_sse_feed(&sse, stream, sizeof(stream) - 17) == AGENT_OK);
    assert(agent_sse_finish(&sse) == AGENT_ERR_PROTOCOL);
    agent_llm_reply_init(&reply); agent_sse_init(&sse, frame, sizeof(frame), &reply, NULL, NULL);
    assert(agent_sse_feed(&sse, "data: [DONE]\n\n", 14) == AGENT_ERR_PROTOCOL);
    agent_llm_reply_init(&reply); agent_sse_init(&sse, frame, sizeof(frame), &reply, NULL, NULL);
    for (unsigned i = 0; i < AGENT_STREAM_MAX; ++i) assert(agent_sse_feed(&sse, "x", 1) == AGENT_OK);
    assert(agent_sse_feed(&sse, "x", 1) == AGENT_ERR_LIMIT);
    memset(frame, 'q', sizeof(frame));
    agent_sse_init(&sse, frame, sizeof(frame), &reply, NULL, NULL);
    for (size_t i=0;i<sizeof(frame);++i) assert(frame[i]=='q');
    memcpy(frame,"request",8);
    agent_llm_reply_init(&reply);
    agent_sse_init(&sse, frame, sizeof(frame), &reply, NULL, NULL);
    agent_http_request_t request={.body=frame,.length=7};
    size_t sent=0;
    assert(!agent_http_write_body(&request,collect_request,&sent) && sent==7);
    assert(!agent_sse_feed(&sse,stream,sizeof(stream)-1));
    assert(!agent_sse_finish(&sse));
    frame[0]='q';
    agent_sse_init(&sse, frame, sizeof(frame)-1, &reply, NULL, NULL);
    assert(agent_sse_feed(&sse, "x", 1)==AGENT_ERR_LIMIT);
    assert(agent_sse_finish(&sse)==AGENT_ERR_LIMIT && frame[0]=='q');
    agent_sse_init(&sse, NULL, sizeof(frame), &reply, NULL, NULL);
    assert(agent_sse_finish(&sse)==AGENT_ERR_LIMIT);
    puts("sse: 1/2/3/7/31-byte and 200 random splits, UTF-8, multiline/CRLF, interleaved tools, truncation and limits PASS");
    return 0;
}
