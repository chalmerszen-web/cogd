#ifndef TEST_WS_SDK_INTERNAL_H
#define TEST_WS_SDK_INTERNAL_H
#include "esp_transport.h"
/* Host-only transport plumbing. The unmodified vendor WS reader and its full
 * public declarations are compiled; TLS/socket I/O is a scripted byte source. */
struct esp_transport_item_t {
    void *data,*foundation;
    int (*_get_socket)(esp_transport_handle_t);
};
#define ESP_TRANSPORT_MEM_CHECK(tag, value, action) if(!(value)) { action; }
#define ESP_TRANSPORT_ERR_OK_CHECK(tag, value, action) if((value)!=ESP_OK) { action; }
int esp_transport_poll_connection_closed(esp_transport_handle_t,int);
#endif
