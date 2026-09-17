#ifndef ESP_HI_CONTROL_BOARD_H
#define ESP_HI_CONTROL_BOARD_H
#include "devices.h"
extern agent_control_t esp_hi_control;
extern agent_devices_t esp_hi_devices;
agent_err_t esp_hi_control_init(void);
unsigned esp_hi_control_stack(void);
/* LCD owns the group until stopped; release leaves these pins as inputs. */
agent_err_t esp_hi_display_pins(bool claim,bool *held);
/* Exclude DMA jobs while a context bank is erased. Ordinary appends stay concurrent. */
agent_err_t esp_hi_storage_begin(void);
void esp_hi_storage_end(void);
#endif
