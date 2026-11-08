#pragma once
#include "qianwen.h"
/* Compatibility callbacks for the shared speech provider. Their bytes travel
 * on native AOQ tracks; no WebSocket connection is opened by these adapters. */
extern const agent_ws_ops_t esp_agent_aoq_asr,esp_agent_aoq_tts;
