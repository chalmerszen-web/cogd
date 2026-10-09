#ifndef TEST_ESP_WIFI_H
#define TEST_ESP_WIFI_H
#include <stdint.h>
typedef int esp_err_t;
enum { ESP_OK=0 };
typedef struct { int8_t rssi; } wifi_ap_record_t;
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
esp_err_t esp_wifi_get_max_tx_power(int8_t *);
esp_err_t esp_wifi_set_max_tx_power(int8_t);
#endif
