#include "hardware.h"
#include <stdio.h>
const char esp_hi_hardware_prompt[]=
    "This is Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3, ESP32-P4 rev3.2 with 16MiB flash. "
    "WiFi is ESP32-C6 over SDIO on GPIO14..19, reset54. I2C SDA7 SCL8. "
    "Audio ES7210 microphones and ES8311 speaker: MCLK13 BCLK12 WS10 DIN11 DOUT9 PA53. "
    "LCD ST7701 480x800 MIPI DSI, backlight26 reset27; GT911 touch. "
    "No WS2812 RGB lights and no towel-machine actuators are installed. "
    "Generic GPIO and LED actions are unavailable. Never claim to execute them. "
    "Use the supported speaker and display tools.";
agent_err_t esp_hi_hardware_status(void *ctx,char *out,size_t cap)
{
    (void)ctx;
    int n=snprintf(out,cap,"{\"board\":\"ESP32-P4-WIFI6-Touch-LCD-4.3\",\"chip\":\"ESP32-P4\","
        "\"flash_bytes\":16777216,\"audio\":\"ES7210+ES8311\",\"wifi\":\"ESP32-C6 SDIO 2.4GHz\","
        "\"display\":\"ST7701 480x800\",\"rgb_leds\":0,\"actuators\":[]}");
    return n<0 || (size_t)n>=cap?AGENT_ERR_LIMIT:AGENT_OK;
}
