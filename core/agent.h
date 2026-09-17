#ifndef AGENT_H
#define AGENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

_Static_assert(__STDC_VERSION__ == 201112L, "Project firmware must compile as C11");

#define AGENT_ABI_MAJOR 5
#define AGENT_INPUT_MAX 2048u
#define AGENT_REQUEST_MAX 24576u
#define AGENT_HTTP_REQUEST_MAX (160u * 1024u)
#define AGENT_STREAM_MAX 6144u
#define AGENT_ARGS_MAX 4096u
#define AGENT_ANSWER_MAX 8192u
#define AGENT_TOOLS_MAX 4u
#define AGENT_ROUNDS_MAX 4u
#define AGENT_PLUGINS_MAX 16u

typedef enum {
    AGENT_OK, AGENT_ERR_ARGUMENT, AGENT_ERR_CONFIG, AGENT_ERR_BUSY,
    AGENT_ERR_CANCELLED, AGENT_ERR_LIMIT, AGENT_ERR_JSON, AGENT_ERR_PROTOCOL,
    AGENT_ERR_NETWORK, AGENT_ERR_DNS, AGENT_ERR_TLS, AGENT_ERR_AUTH,
    AGENT_ERR_FORBIDDEN, AGENT_ERR_RATE, AGENT_ERR_SERVER, AGENT_ERR_TIMEOUT,
    AGENT_ERR_STORAGE, AGENT_ERR_FULL, AGENT_ERR_CORRUPT, AGENT_ERR_MEMORY,
    AGENT_ERR_TOOL, AGENT_ERR_ABI, AGENT_ERR_DUPLICATE, AGENT_ERR_NOT_FOUND,
    AGENT_ERR_OFFLINE
} agent_err_t;

typedef agent_err_t (*agent_write_fn)(void *ctx, const char *data, size_t length);
typedef agent_err_t (*agent_body_fn)(void *ctx, agent_write_fn write, void *write_ctx);

typedef enum {
    AGENT_PLATFORM, AGENT_TRANSPORT, AGENT_LLM, AGENT_CONTEXT_STORE,
    AGENT_CONTEXT_SYNC, AGENT_AUTH, AGENT_INPUT, AGENT_OUTPUT, AGENT_TOOL,
    AGENT_TELEMETRY, AGENT_MODEL_RUNTIME
} agent_plugin_kind_t;

typedef enum {
    AGENT_EVENT_INPUT, AGENT_EVENT_TEXT, AGENT_EVENT_REASONING,
    AGENT_EVENT_TOOL_BEGIN, AGENT_EVENT_TOOL_ARGUMENT, AGENT_EVENT_TOOL_END,
    AGENT_EVENT_DONE, AGENT_EVENT_ERROR
} agent_event_type_t;

/* Borrowed data: valid for this synchronous dispatch only. */
typedef struct {
    agent_event_type_t type;
    const char *data;
    size_t length;
    unsigned index;
    agent_err_t error;
} agent_event_t;

typedef struct {
    uint64_t (*monotonic_ms)(void *ctx);
    agent_err_t (*random_bytes)(void *ctx, void *dst, size_t size);
    agent_err_t (*kv_get)(void *ctx, const char *key, void *dst, size_t *size);
    agent_err_t (*kv_set)(void *ctx, const char *key, const void *src, size_t size);
    void *ctx;
} agent_platform_ops_t;

typedef struct {
    agent_err_t (*read)(void *ctx, char *data, size_t capacity, size_t *length);
    void *ctx;
} agent_input_ops_t;
typedef struct {
    void (*write)(void *ctx, const char *data, size_t length);
    void *ctx;
} agent_output_ops_t;

typedef struct agent_plugin {
    uint16_t abi_major, abi_minor;
    uint32_t struct_size;
    agent_plugin_kind_t kind;
    const char *name;
    const void *ops;
    void *ctx;
    agent_err_t (*init)(void *ctx);
    agent_err_t (*start)(void *ctx);
    agent_err_t (*handle_event)(void *ctx, const agent_event_t *event);
    void (*stop)(void *ctx);
    void (*deinit)(void *ctx);
} agent_plugin_t;

#define AGENT_PLUGIN(kind_, name_, ops_, ctx_) \
    { .abi_major = AGENT_ABI_MAJOR, .struct_size = sizeof(agent_plugin_t), \
      .kind = kind_, .name = name_, .ops = ops_, .ctx = ctx_ }

typedef struct {
    const agent_plugin_t *plugins[AGENT_PLUGINS_MAX];
    size_t count, initialized, started;
    atomic_bool busy, cancelled;
    unsigned tool_rounds;
} agent_core_t;

const char *agent_err_name(agent_err_t error);
void agent_core_init(agent_core_t *core);
agent_err_t agent_register(agent_core_t *core, const agent_plugin_t *plugin);
const agent_plugin_t *agent_find(const agent_core_t *core, const char *name);
agent_err_t agent_start(agent_core_t *core);
void agent_stop(agent_core_t *core);
agent_err_t agent_dispatch(agent_core_t *core, const agent_event_t *event);
agent_err_t agent_begin_turn(agent_core_t *core);
agent_err_t agent_next_round(agent_core_t *core);
void agent_cancel(agent_core_t *core);
void agent_end_turn(agent_core_t *core);

#endif
