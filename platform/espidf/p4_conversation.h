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
/* AOQ diagnostic setting, between sessions. 50..1000 means 0.05..1.0. */
agent_err_t esp_agent_conversation_vad_threshold(unsigned thousandths);
/* Diagnostic-only AOQ replay pacing, between sessions: 5, 10 or 20 ms. */
agent_err_t esp_agent_conversation_catchup(unsigned frame_interval_ms);
/* AOQ multimodal input; change only with the conversation stopped. */
agent_err_t esp_agent_conversation_video(bool enabled);
/* Probe builds only, volatile and off at boot; change with voice stopped. */
agent_err_t esp_agent_conversation_test_tools(bool enabled);
#if defined(AGENT_P4_AOQ_PROBE)
/* Native AOQ probe only: replace the next 0..2 matched nonempty ASR finals
 * with empty finals, between sessions. Volatile, disabled on boot. */
agent_err_t esp_agent_conversation_test_empty(unsigned count);
#endif
#endif
