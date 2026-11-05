#include "board.h"
#include "esp_pm.h"
/* The P4 board has a power indicator, no addressable RGB strip. */
agent_err_t esp_hi_light_init(void)
{
    /* Fixed 200 MHz bring-up profile. 400 MHz showed intermittent access
     * faults on this rev3.2 unit; retain the diagnostic logs in evidence/. */
    const esp_pm_config_t cpu={.max_freq_mhz=200,.min_freq_mhz=200,.light_sleep_enable=false};
    return esp_pm_configure(&cpu)==ESP_OK?AGENT_OK:AGENT_ERR_CONFIG;
}
agent_err_t esp_hi_light_get(void *ctx,uint8_t rgb[3])
{(void)ctx;(void)rgb;return AGENT_ERR_FORBIDDEN;}
agent_err_t esp_hi_light_set(void *ctx,const uint8_t rgb[3])
{(void)ctx;(void)rgb;return AGENT_ERR_FORBIDDEN;}


