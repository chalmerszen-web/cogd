#ifndef ESP_AGENT_WS_CONNECT_CLOCK_H
#define ESP_AGENT_WS_CONNECT_CLOCK_H
#include "esp_transport.h"

/* One candidate task registers its live TLS handle, calls WS.connect, then
 * unregisters and reads. Unrelated transports are delegated unchanged. */
void esp_agent_ws_connect_watch(esp_transport_handle_t tls);
void esp_agent_ws_connect_clock(unsigned *began,unsigned *ended);
#endif
