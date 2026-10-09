#ifndef TEST_ESP_FREERTOS_HOOKS_H
#define TEST_ESP_FREERTOS_HOOKS_H
#include <stdbool.h>
#include "esp_err.h"
typedef bool (*esp_freertos_idle_cb_t)(void);
esp_err_t esp_register_freertos_idle_hook_for_cpu(esp_freertos_idle_cb_t,unsigned);
#endif
