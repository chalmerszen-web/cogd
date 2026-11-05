#ifndef ESP_AGENT_P4_CONVERSATION_H
#define ESP_AGENT_P4_CONVERSATION_H
#include "tools.h"
#include "context.h"
/* Run by the existing network owner while it owns the context/work lock.
 * Slow tools have a separate bounded worker and survive speech cancellation. */
agent_err_t esp_agent_conversation_run(const agent_tool_ops_t *,agent_context_t *,
    const atomic_bool *,void (*event)(void *,const char *,const char *));
void esp_agent_conversation_status(char *,size_t);
/* Volatile A/B setting, between sessions: 0 semantic, 200..800 ms acoustic. */
agent_err_t esp_agent_conversation_endpoint(unsigned silence_ms);
#endif
