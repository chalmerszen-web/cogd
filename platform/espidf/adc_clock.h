#ifndef ESP_AGENT_ADC_CLOCK_H
#define ESP_AGENT_ADC_CLOCK_H
#include "agent.h"
#include <stdbool.h>
#include <stddef.h>

/* Opt-in C3 adapter: applied is configuration, active is live timer state. */
void esp_agent_adc_clock_reset(void);
bool esp_agent_adc_clock_applied(unsigned requested_hz);
agent_err_t esp_agent_adc_clock_status(char *output,size_t capacity);
#endif
