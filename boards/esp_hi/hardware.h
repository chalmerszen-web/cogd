#ifndef ESP_HI_HARDWARE_H
#define ESP_HI_HARDWARE_H
#include "agent.h"
extern const char esp_hi_hardware_prompt[];
agent_err_t esp_hi_hardware_status(void *,char *,size_t);
#endif
