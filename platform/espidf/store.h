#ifndef AGENT_ESP_STORE_H
#define AGENT_ESP_STORE_H
#include "context.h"
void esp_agent_store_metrics(unsigned out[4]);
#include "nvs.h"
agent_err_t esp_agent_store_open(agent_context_t *,nvs_handle_t,bool initialize);
#endif
