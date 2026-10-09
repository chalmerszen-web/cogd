#ifndef AGENT_LLM_H
#define AGENT_LLM_H
#include "agent.h"

#define AGENT_HISTORY_MAX 16384u
typedef struct {
    char id[129], name[129];
    size_t offset, length;
    bool present;
} agent_tool_call_t;

typedef struct {
    char text[AGENT_ANSWER_MAX + 1];
    size_t text_length, args_used;
    char arguments[AGENT_ARGS_MAX + AGENT_TOOLS_MAX];
    agent_tool_call_t calls[AGENT_TOOLS_MAX];
    unsigned call_count;
    bool finished, done;
    agent_err_t error;
} agent_llm_reply_t;

typedef struct { char data[AGENT_HISTORY_MAX + 1]; size_t used,system_used; } agent_messages_t;
typedef agent_err_t (*agent_emit_fn)(void *ctx, const agent_event_t *event);

agent_err_t agent_messages_init(agent_messages_t *messages, const char *system);
agent_err_t agent_messages_add(agent_messages_t *messages, const char *role, const char *content, const char *call_id);
/* One complete ordinary message object. No surrounding array/comma; writes
 * synchronously and stops on the first error, with no allocation or retry. */
agent_err_t agent_message_write(const char *role,const char *content,const char *call_id,agent_write_fn,void *);
agent_err_t agent_messages_assistant(agent_messages_t *messages, const agent_llm_reply_t *reply);
agent_err_t agent_messages_raw(agent_messages_t *messages, const char *json);
agent_err_t agent_deepseek_request(const agent_messages_t *messages, bool stream, const char *tools_json,
                                 char *output, size_t capacity, size_t *length);
agent_err_t agent_deepseek_compose(bool stream,agent_body_fn messages,void *messages_ctx,
                                  agent_body_fn tools,void *tools_ctx,agent_write_fn write,void *write_ctx);
agent_err_t agent_deepseek_compose_final(bool stream,agent_body_fn messages,void *messages_ctx,
                                       agent_write_fn write,void *write_ctx);
void agent_llm_reply_init(agent_llm_reply_t *reply);
agent_err_t agent_llm_parse(agent_llm_reply_t *reply, const char *data, size_t length,
                           bool delta, agent_emit_fn emit, void *ctx);
const char *agent_call_arguments(const agent_llm_reply_t *reply, unsigned index);

#endif
