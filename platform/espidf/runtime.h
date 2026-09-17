#ifndef AGENT_ESP_RUNTIME_H
#define AGENT_ESP_RUNTIME_H
#include "agent.h"
#include "transport.h"

void esp_agent_run(agent_core_t *core);
void esp_agent_write(const char *data, size_t size);
void esp_agent_status(char *output, size_t capacity);
uint64_t esp_agent_now(void);
bool esp_agent_online(void);
agent_err_t esp_agent_http_auth(agent_http_target_t target, char *base, size_t base_size,
                               char *auth, size_t auth_size, const char **ca);
agent_err_t esp_agent_http(void *ctx, const agent_http_request_t *request, agent_http_feed_fn feed, void *feed_ctx);

#endif
