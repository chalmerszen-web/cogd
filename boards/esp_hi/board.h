#ifndef ESP_HI_BOARD_H
#define ESP_HI_BOARD_H
#include "agent.h"
agent_err_t esp_hi_light_init(void);
agent_err_t esp_hi_light_get(void *ctx, uint8_t rgb[3]);
agent_err_t esp_hi_light_set(void *ctx, const uint8_t rgb[3]);
#endif
