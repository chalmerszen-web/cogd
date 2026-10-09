#ifndef AGENT_CAPTURE_RADIO_H
#define AGENT_CAPTURE_RADIO_H
#include "agent.h"
typedef struct { int previous,applied,rssi; } esp_agent_capture_radio_t;
/* Network worker only. Temporary strong-link ADC-noise mitigation. No NVS.
 * Always end after capture joins, including begin/send/cancel failures. */
agent_err_t esp_agent_capture_radio_begin(esp_agent_capture_radio_t *);
/* Idempotent. A failed restoration retains its original value for retry. */
agent_err_t esp_agent_capture_radio_end(void);
#endif
