#ifndef TEST_WS_ADAPTER_TLS_H
#define TEST_WS_ADAPTER_TLS_H
#include "esp_transport.h"
#include <stdint.h>
typedef int esp_tls_dyn_buf_strategy_t;
typedef int esp_tls_proto_ver_t;
typedef int esp_tls_ecdsa_curve_t;
typedef int esp_tls_addr_family_t;
struct ifreq;
typedef struct test_key_config esp_key_config_t;
typedef struct test_psk_key psk_hint_key_t;
enum { ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME=10, ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT,
       ESP_ERR_ESP_TLS_SERVER_HANDSHAKE_TIMEOUT };
esp_err_t esp_tls_get_and_clear_last_error(esp_tls_error_handle_t,int *,int *);
#endif
