#include "capture_radio.h"
#include "esp_wifi.h"

enum { CAPTURE_POWER=8, STRONG_RSSI=-55 }; /* quarter dBm; dBm */
static int restore_power=-1;

agent_err_t esp_agent_capture_radio_begin(esp_agent_capture_radio_t *status)
{
    if(!status)return AGENT_ERR_ARGUMENT;
    if(restore_power>=0)return AGENT_ERR_BUSY;
    wifi_ap_record_t ap;int8_t power;
    if(esp_wifi_sta_get_ap_info(&ap)!=ESP_OK || esp_wifi_get_max_tx_power(&power)!=ESP_OK)
        return AGENT_ERR_NETWORK;
    *status=(esp_agent_capture_radio_t){power,power,ap.rssi};
    /* Weak or already limited links retain the caller's power budget. RSSI
     * is an admission guard, not proof of uplink quality at another location. */
    if(ap.rssi<STRONG_RSSI || power<=CAPTURE_POWER)return AGENT_OK;
    restore_power=power; /* Keep ownership even if set/readback fails. */
    if(esp_wifi_set_max_tx_power(CAPTURE_POWER)!=ESP_OK ||
       esp_wifi_get_max_tx_power(&power)!=ESP_OK || power!=CAPTURE_POWER)
        return AGENT_ERR_CONFIG;
    status->applied=power;return AGENT_OK;
}

agent_err_t esp_agent_capture_radio_end(void)
{
    if(restore_power<0)return AGENT_OK;
    int8_t power;
    if(esp_wifi_set_max_tx_power((int8_t)restore_power)!=ESP_OK ||
       esp_wifi_get_max_tx_power(&power)!=ESP_OK || power!=restore_power)
        return AGENT_ERR_CONFIG;
    restore_power=-1;return AGENT_OK;
}
