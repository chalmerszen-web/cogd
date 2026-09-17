#include "llm.h"
#include "json.h"
#include <stdio.h>
#include <string.h>

static void start_message(agent_messages_t *messages, agent_json_writer_t *w)
{
    agent_json_writer_init(w, messages->data + messages->used - 1,
                           sizeof(messages->data) - messages->used + 1);
    agent_json_raw(w, messages->used > 2 ? ",{" : "{");
}

static agent_err_t end_message(agent_messages_t *messages, agent_json_writer_t *w)
{
    agent_json_raw(w, "}]");
    if (!w->error) messages->used += w->used - 1;
    else { messages->data[messages->used - 1] = ']'; messages->data[messages->used] = 0; }
    return w->error;
}

agent_err_t agent_messages_init(agent_messages_t *messages, const char *system)
{
    strcpy(messages->data, "[]"); messages->used = 2;
    agent_err_t error=agent_messages_add(messages,"system",system,NULL);
    messages->system_used=messages->used; return error;
}

agent_err_t agent_messages_raw(agent_messages_t *messages, const char *json)
{
    size_t n=strlen(json), extra=n+1;
    if(extra>AGENT_HISTORY_MAX-messages->used) return AGENT_ERR_LIMIT;
    messages->data[messages->used-1]=',';
    memcpy(messages->data+messages->used,json,n);
    messages->used+=extra; messages->data[messages->used-1]=']'; messages->data[messages->used]=0;
    return AGENT_OK;
}

agent_err_t agent_messages_add(agent_messages_t *messages, const char *role, const char *content, const char *call_id)
{
    agent_json_writer_t w;
    start_message(messages, &w);
    agent_json_raw(&w, "\"role\":"); agent_json_quote(&w, role);
    agent_json_raw(&w, ",\"content\":"); agent_json_quote(&w, content);
    if (call_id) { agent_json_raw(&w, ",\"tool_call_id\":"); agent_json_quote(&w, call_id); }
    return end_message(messages, &w);
}

const char *agent_call_arguments(const agent_llm_reply_t *reply, unsigned index)
{
    return index < reply->call_count ? reply->arguments + reply->calls[index].offset : NULL;
}

agent_err_t agent_messages_assistant(agent_messages_t *messages, const agent_llm_reply_t *reply)
{
    agent_json_writer_t w;
    start_message(messages, &w);
    agent_json_raw(&w, "\"role\":\"assistant\",\"content\":"); agent_json_quote(&w, reply->text);
    if (reply->call_count) {
        agent_json_raw(&w, ",\"tool_calls\":[");
        for (unsigned i = 0; i < reply->call_count; ++i) {
            if (i) agent_json_raw(&w, ",");
            agent_json_raw(&w, "{\"id\":"); agent_json_quote(&w, reply->calls[i].id);
            agent_json_raw(&w, ",\"type\":\"function\",\"function\":{\"name\":");
            agent_json_quote(&w, reply->calls[i].name);
            agent_json_raw(&w, ",\"arguments\":"); agent_json_quote(&w, agent_call_arguments(reply, i));
            agent_json_raw(&w, "}}");
        }
        agent_json_raw(&w, "]");
    }
    return end_message(messages, &w);
}

agent_err_t agent_deepseek_compose(bool stream,agent_body_fn messages,void *messages_ctx,
                                  agent_body_fn tools,void *tools_ctx,agent_write_fn write,void *write_ctx)
{
    if (!messages || !write) return AGENT_ERR_ARGUMENT;
    char prefix[192];
    int n = snprintf(prefix, sizeof(prefix), "{\"model\":\"deepseek-flash\",\"thinking\":{\"type\":\"disabled\"},"
                         "\"stream\":%s,\"max_tokens\":%u,\"messages\":", stream ? "true" : "false",
#if AGENT_ENABLE_AUDIO
                         4096u
#else
                         512u
#endif
    );
    if (n < 0 || (size_t)n >= sizeof(prefix)) return AGENT_ERR_LIMIT;
    agent_err_t error = write(write_ctx, prefix, (size_t)n);
    if (!error) error = messages(messages_ctx, write, write_ctx);
    if (!error && tools) error = write(write_ctx, ",\"tools\":", 9);
    if (!error && tools) error = tools(tools_ctx, write, write_ctx);
    return error ? error : write(write_ctx, "}", 1);
}

static agent_err_t message_value(void *ctx, agent_write_fn write, void *write_ctx)
{
    const agent_messages_t *m = ctx;
    return write(write_ctx, m->data, m->used);
}
static agent_err_t json_value(void *ctx,agent_write_fn write,void *write_ctx)
{ return write(write_ctx,ctx,strlen(ctx)); }

static agent_err_t request_bytes(void *ctx, const char *data, size_t n)
{
    agent_json_writer_t *w = ctx;
    if (w->error) return w->error;
    if (n >= w->capacity - w->used) return w->error = AGENT_ERR_LIMIT;
    memcpy(w->data + w->used, data, n); w->used += n; w->data[w->used] = 0;
    return AGENT_OK;
}

agent_err_t agent_deepseek_request(const agent_messages_t *messages, bool stream, const char *tools_json,
                                 char *output, size_t capacity, size_t *length)
{
    if (!messages || !output || !length) return AGENT_ERR_ARGUMENT;
    if (capacity > AGENT_REQUEST_MAX + 1) capacity = AGENT_REQUEST_MAX + 1;
    agent_json_writer_t w; agent_json_writer_init(&w, output, capacity);
    agent_err_t error = agent_deepseek_compose(stream,message_value,(void *)messages,
        tools_json?json_value:NULL,(void *)tools_json,request_bytes,&w);
    *length = w.used;
    return error;
}

void agent_llm_reply_init(agent_llm_reply_t *reply) { memset(reply, 0, sizeof(*reply)); }

static agent_err_t emit_event(agent_emit_fn emit, void *ctx, agent_event_type_t type,
                              const char *data, unsigned index)
{
    agent_event_t event = { .type = type, .data = data, .length = data ? strlen(data) : 0, .index = index };
    return emit ? emit(ctx, &event) : AGENT_OK;
}

static agent_err_t append_text(char *dst, size_t capacity, const char *src)
{
    size_t used = strlen(dst), size = strlen(src);
    if (size >= capacity - used) return AGENT_ERR_LIMIT;
    memcpy(dst + used, src, size + 1);
    return AGENT_OK;
}

static agent_err_t call_delta(agent_llm_reply_t *reply, const cJSON *value, unsigned index,
                              agent_emit_fn emit, void *ctx)
{
    if (index >= AGENT_TOOLS_MAX) return AGENT_ERR_LIMIT;
    agent_tool_call_t *call = &reply->calls[index];
    if (!call->present) {
        if (reply->args_used >= sizeof(reply->arguments)) return AGENT_ERR_LIMIT;
        call->present = true;
        call->offset = reply->args_used++;
        reply->arguments[call->offset] = 0;
        if (index + 1 > reply->call_count) reply->call_count = index + 1;
        agent_err_t error = emit_event(emit, ctx, AGENT_EVENT_TOOL_BEGIN, NULL, index);
        if (error) return error;
    }
    const char *id = agent_json_string(value, "id");
    const char *type = agent_json_string(value, "type");
    if((cJSON_GetObjectItemCaseSensitive(value,"id") && !id) || (cJSON_GetObjectItemCaseSensitive(value,"type") && !type)) return AGENT_ERR_PROTOCOL;
    if (type && strcmp(type, "function")) return AGENT_ERR_PROTOCOL;
    if (id && append_text(call->id, sizeof(call->id), id)) return AGENT_ERR_LIMIT;
    const cJSON *function = cJSON_GetObjectItemCaseSensitive(value, "function");
    if (!function) return AGENT_OK;
    if (!cJSON_IsObject(function)) return AGENT_ERR_PROTOCOL;
    const char *name = agent_json_string(function, "name");
    if(cJSON_GetObjectItemCaseSensitive(function,"name") && !name) return AGENT_ERR_PROTOCOL;
    if (name && append_text(call->name, sizeof(call->name), name)) return AGENT_ERR_LIMIT;
    const char *args = agent_json_string(function, "arguments");
    if(cJSON_GetObjectItemCaseSensitive(function,"arguments") && !args) return AGENT_ERR_PROTOCOL;
    if (!args) return AGENT_OK;
    size_t size = strlen(args), end = call->offset + call->length;
    unsigned present = 0;
    for (unsigned i = 0; i < reply->call_count; ++i) present += reply->calls[i].present;
    if (size > AGENT_ARGS_MAX - (reply->args_used - present)) return AGENT_ERR_LIMIT;
    memmove(reply->arguments + end + size, reply->arguments + end, reply->args_used - end);
    memcpy(reply->arguments + end, args, size);
    for (unsigned i = 0; i < reply->call_count; ++i)
        if (i != index && reply->calls[i].present && reply->calls[i].offset > end) reply->calls[i].offset += size;
    reply->args_used += size; call->length += size;
    return emit_event(emit, ctx, AGENT_EVENT_TOOL_ARGUMENT, args, index);
}

agent_err_t agent_llm_parse(agent_llm_reply_t *reply, const char *data, size_t length,
                           bool delta, agent_emit_fn emit, void *ctx)
{
    if (reply->error) return reply->error;
    cJSON *root = agent_json_parse(data, length);
    agent_err_t error = AGENT_ERR_PROTOCOL;
    if (!root) return reply->error = AGENT_ERR_JSON;
    const cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    if (!cJSON_IsArray(choices)) goto done;
    if (!choices->child && delta) { error = AGENT_OK; goto done; }
    if (reply->finished) goto done;
    const cJSON *choice = cJSON_GetArrayItem(choices, 0);
    if (!cJSON_IsObject(choice) || choice->next) goto done;
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(choice, delta ? "delta" : "message");
    if (!cJSON_IsObject(message)) goto done;
    const cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
    if (content && !cJSON_IsString(content) && !cJSON_IsNull(content)) goto done;
    if (cJSON_IsString(content)) {
        error = append_text(reply->text, sizeof(reply->text), content->valuestring);
        if (error) goto done;
        reply->text_length = strlen(reply->text);
        error = emit_event(emit, ctx, AGENT_EVENT_TEXT, content->valuestring, 0);
        if (error) goto done;
    }
    const char *reasoning = agent_json_string(message, "reasoning_content");
    if (reasoning && (error = emit_event(emit, ctx, AGENT_EVENT_REASONING, reasoning, 0))) goto done;
    const cJSON *calls = cJSON_GetObjectItemCaseSensitive(message, "tool_calls");
    if (calls && !cJSON_IsNull(calls) && !cJSON_IsArray(calls)) { error = AGENT_ERR_PROTOCOL; goto done; }
    unsigned position = 0;
    for (const cJSON *call = calls ? calls->child : NULL; call; call = call->next, ++position) {
        uint64_t index = position;
        if (delta && !agent_json_uint(cJSON_GetObjectItemCaseSensitive(call, "index"), AGENT_TOOLS_MAX - 1, &index)) {
            error = AGENT_ERR_LIMIT; goto done;
        }
        error = call_delta(reply, call, (unsigned)index, emit, ctx);
        if (error) goto done;
    }
    error = AGENT_OK;
    const char *finish = agent_json_string(choice, "finish_reason");
    const cJSON *finish_value=cJSON_GetObjectItemCaseSensitive(choice,"finish_reason");
    if(finish_value && !cJSON_IsNull(finish_value) && !finish) { error=AGENT_ERR_PROTOCOL; goto done; }
    if (finish) {
        reply->finished = true;
        if (!strcmp(finish, "length")) error = AGENT_ERR_LIMIT;
        else if (!strcmp(finish, "content_filter")) error = AGENT_ERR_FORBIDDEN;
        else if (strcmp(finish, "stop") && strcmp(finish, "tool_calls")) error = AGENT_ERR_SERVER;
        else if ((!strcmp(finish, "tool_calls")) != (reply->call_count > 0)) error = AGENT_ERR_PROTOCOL;
        for (unsigned i = 0; !error && i < reply->call_count; ++i) {
            if (!reply->calls[i].present || !reply->calls[i].id[0] || !reply->calls[i].name[0]) error = AGENT_ERR_PROTOCOL;
            else error = emit_event(emit, ctx, AGENT_EVENT_TOOL_END, NULL, i);
        }
    }
    if (!delta && !reply->finished) error = AGENT_ERR_PROTOCOL;
    if (!delta && !error) reply->done = true;
done:
    cJSON_Delete(root);
    if (error) reply->error = error;
    return error;
}
