#include "capture_radio.h"
#include "esp_wifi.h"
#include <assert.h>
#include <stdio.h>

static int8_t power=80,rssi=-40;
static bool get_failed,set_failed,ap_failed,readback_wrong;
static unsigned sets;
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap)
{ ap->rssi=rssi;return ap_failed?-1:ESP_OK; }
esp_err_t esp_wifi_get_max_tx_power(int8_t *value)
{ *value=readback_wrong?44:power;return get_failed?-1:ESP_OK; }
esp_err_t esp_wifi_set_max_tx_power(int8_t value)
{ ++sets;if(set_failed)return -1;power=value;return ESP_OK; }

int main(void)
{
    esp_agent_capture_radio_t state;
    assert(!esp_agent_capture_radio_end());
    assert(esp_agent_capture_radio_begin(NULL)==AGENT_ERR_ARGUMENT);
    assert(!esp_agent_capture_radio_begin(&state));
    assert(power==8 && state.previous==80 && state.applied==8 && state.rssi==-40);
    assert(esp_agent_capture_radio_begin(&state)==AGENT_ERR_BUSY);
    assert(!esp_agent_capture_radio_end() && power==80);
    assert(!esp_agent_capture_radio_end() && sets==2);
    rssi=-56;assert(!esp_agent_capture_radio_begin(&state));
    assert(state.previous==80 && state.applied==80 && power==80 && sets==2);
    assert(!esp_agent_capture_radio_end());
    rssi=-55;power=44;assert(!esp_agent_capture_radio_begin(&state) && power==8);
    set_failed=true;assert(esp_agent_capture_radio_end()==AGENT_ERR_CONFIG);
    assert(esp_agent_capture_radio_begin(&state)==AGENT_ERR_BUSY);
    set_failed=false;assert(!esp_agent_capture_radio_end() && power==44);
    power=8;unsigned before=sets;assert(!esp_agent_capture_radio_begin(&state));
    assert(!esp_agent_capture_radio_end() && sets==before);
    power=80;ap_failed=true;assert(esp_agent_capture_radio_begin(&state)==AGENT_ERR_NETWORK);
    assert(!esp_agent_capture_radio_end() && power==80);ap_failed=false;
    get_failed=true;assert(esp_agent_capture_radio_begin(&state)==AGENT_ERR_NETWORK);get_failed=false;
    set_failed=true;assert(esp_agent_capture_radio_begin(&state)==AGENT_ERR_CONFIG);
    set_failed=false;assert(!esp_agent_capture_radio_end() && power==80);
    assert(!esp_agent_capture_radio_begin(&state));
    readback_wrong=true;assert(esp_agent_capture_radio_end()==AGENT_ERR_CONFIG);
    readback_wrong=false;assert(!esp_agent_capture_radio_end() && power==80);
    puts("capture radio: strong/weak link, exact prior power, idempotence and retained restoration failures OK");
}
