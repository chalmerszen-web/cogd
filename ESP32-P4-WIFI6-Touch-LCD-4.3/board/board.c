#include "board.h"
/* The P4 board has a power indicator, no addressable RGB strip. */
agent_err_t esp_hi_light_init(void) {return AGENT_OK;}
agent_err_t esp_hi_light_get(void *ctx,uint8_t rgb[3])
{(void)ctx;(void)rgb;return AGENT_ERR_FORBIDDEN;}
agent_err_t esp_hi_light_set(void *ctx,const uint8_t rgb[3])
{(void)ctx;(void)rgb;return AGENT_ERR_FORBIDDEN;}
