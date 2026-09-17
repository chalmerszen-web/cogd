#ifndef ESP_HI_DISPLAY_BOARD_H
#define ESP_HI_DISPLAY_BOARD_H
#include "display.h"
extern const agent_display_ops_t esp_hi_display_ops;
agent_err_t esp_hi_display_init(void);
void esp_hi_display_tick(uint64_t now_ms);
void esp_hi_display_cancel(void);
#endif
