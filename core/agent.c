#include "agent.h"
#include <string.h>

const char *agent_err_name(agent_err_t error)
{
    static const char *const names[] = {
        "ok", "argument", "config", "busy", "cancelled", "limit", "json",
        "protocol", "network", "dns", "tls", "auth", "forbidden", "rate",
        "server", "timeout", "storage", "full", "corrupt", "memory", "tool",
        "abi", "duplicate", "not_found", "offline"
    };
    return (unsigned)error < sizeof(names) / sizeof(names[0]) ? names[error] : "unknown";
}

void agent_core_init(agent_core_t *core)
{
    memset(core, 0, sizeof(*core));
    atomic_init(&core->busy, false);
    atomic_init(&core->cancelled, false);
}

const agent_plugin_t *agent_find(const agent_core_t *core, const char *name)
{
    if (!name) return NULL;
    for (size_t i = 0; i < core->count; ++i)
        if (!strcmp(core->plugins[i]->name, name)) return core->plugins[i];
    return NULL;
}

agent_err_t agent_register(agent_core_t *core, const agent_plugin_t *plugin)
{
    if (!plugin || !plugin->name || !plugin->name[0] || (unsigned)plugin->kind>AGENT_MODEL_RUNTIME) return AGENT_ERR_ARGUMENT;
    if (plugin->abi_major != AGENT_ABI_MAJOR || plugin->struct_size < sizeof(*plugin))
        return AGENT_ERR_ABI;
    if (core->initialized || core->started) return AGENT_ERR_BUSY;
    if (agent_find(core, plugin->name)) return AGENT_ERR_DUPLICATE;
    if (core->count == AGENT_PLUGINS_MAX) return AGENT_ERR_LIMIT;
    core->plugins[core->count++] = plugin;
    return AGENT_OK;
}

void agent_stop(agent_core_t *core)
{
    while (core->started) {
        const agent_plugin_t *plugin = core->plugins[--core->started];
        if (plugin->stop) plugin->stop(plugin->ctx);
    }
    while (core->initialized) {
        const agent_plugin_t *plugin = core->plugins[--core->initialized];
        if (plugin->deinit) plugin->deinit(plugin->ctx);
    }
}

agent_err_t agent_start(agent_core_t *core)
{
    if (core->initialized || core->started) return AGENT_ERR_BUSY;
    agent_err_t error = AGENT_OK;
    for (size_t i = 0; i < core->count; ++i) {
        const agent_plugin_t *plugin = core->plugins[i];
        if (plugin->init && (error = plugin->init(plugin->ctx)) != AGENT_OK) goto fail;
        ++core->initialized;
    }
    for (size_t i = 0; i < core->count; ++i) {
        const agent_plugin_t *plugin = core->plugins[i];
        if (plugin->start && (error = plugin->start(plugin->ctx)) != AGENT_OK) goto fail;
        ++core->started;
    }
    return AGENT_OK;
fail:
    agent_stop(core);
    return error;
}

agent_err_t agent_dispatch(agent_core_t *core, const agent_event_t *event)
{
    if (!event || (!event->data && event->length)) return AGENT_ERR_ARGUMENT;
    for (size_t i = 0; i < core->started; ++i) {
        const agent_plugin_t *plugin = core->plugins[i];
        if (plugin->handle_event) {
            agent_err_t error = plugin->handle_event(plugin->ctx, event);
            if (error != AGENT_OK) return error;
        }
    }
    return AGENT_OK;
}

agent_err_t agent_begin_turn(agent_core_t *core)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&core->busy, &expected, true)) return AGENT_ERR_BUSY;
    atomic_store(&core->cancelled, false);
    core->tool_rounds = 0;
    return AGENT_OK;
}

agent_err_t agent_next_round(agent_core_t *core)
{
    if (atomic_load(&core->cancelled)) return AGENT_ERR_CANCELLED;
    if (!atomic_load(&core->busy)) return AGENT_ERR_ARGUMENT;
    if (core->tool_rounds >= AGENT_ROUNDS_MAX) return AGENT_ERR_LIMIT;
    ++core->tool_rounds;
    return AGENT_OK;
}

void agent_cancel(agent_core_t *core) { atomic_store(&core->cancelled, true); }
void agent_end_turn(agent_core_t *core) { atomic_store(&core->busy, false); }
