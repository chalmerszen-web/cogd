#pragma once
#include "cJSON.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#if AGENT_BOARD_P4
/* Synchronous, borrowed JSON. No history, NVS, price estimation or audio copy.
 * Each provider event is reported as-is, never added to earlier usage. */
void agent_model_usage(const char *model, const cJSON *response, bool terminal);
void esp_agent_model_receipt(const char *json);
#else
#define agent_model_usage(model, response, terminal) ((void)(model),(void)(response),(void)(terminal))
#endif
#ifdef __cplusplus
}
#endif
