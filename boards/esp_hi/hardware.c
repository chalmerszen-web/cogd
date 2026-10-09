#include "hardware.h"
#include "json.h"

const char esp_hi_hardware_prompt[]=
    "Current ESP-HI head wiring overrides old history: ESP32-C3 rev0.4,4MiB Flash. "
    "GPIO0=MOVE_WAKE,1=AUDIO_WAKE,9=BOOT: read-only inputs;2=ADC1_CH2 mic;3=PA enable;"
    "6/7=PDM speaker;8=4 WS2812 RGB LEDs. "
    "LCD160x80,SPI2 MOSI4 SCLK5 DC10,mode0; no CS/RST/MISO/backlight control. "
    "Profile esp_hi_st7735_bsp; physical controller unverified. "
    "device_display_set(mode=clock,utc_offset_minutes=480) starts a persistent Beijing clock, "
    "24-hour large HH:MM with small SS at bottom right, refreshed locally each second. "
    "Time from device NTP only; unset --:-- and --, never invent. "
    "Use display tools, not GPIO bit-banging. Display ready/mode/frames prove transport/SPI writes, not visible pixels. "
    "Off releases4/5/10 to GPIO inputs; owner5=display. "
    "20/21=body outputs, not verified free; no servo driver. "
    "18/19=active USB,12..17=Flash,11=unverified: never repurpose. "
    "Use dedicated audio/LED tools; device_hardware_get gives wiring/availability."
#if !AGENT_ENABLE_AUDIO
    " This build has audio disabled even though its physical wiring is listed."
#endif
    ;

agent_err_t esp_hi_hardware_status(void *ctx,char *out,size_t cap)
{
    (void)ctx;agent_json_writer_t w;agent_json_writer_init(&w,out,cap);
    agent_json_raw(&w,"{\"board\":\"ESP-HI head\",\"chip\":\"ESP32-C3 rev0.4\",\"flash_bytes\":4194304,"
        "\"buttons\":[0,1,9],\"led\":{\"gpio\":8,\"type\":\"WS2812\",\"count\":4},"
        "\"audio\":{\"adc_gpio\":2,\"adc_channel\":2,\"pa\":3,\"pdm\":[6,7],\"enabled\":");
    agent_json_raw(&w,AGENT_ENABLE_AUDIO?"true":"false");
    agent_json_raw(&w,"},\"display\":{\"available\":true,\"width\":160,\"height\":80,\"bus\":\"SPI2\","
        "\"mosi\":4,\"sclk\":5,\"dc\":10,\"cs\":null,\"reset\":null,\"miso\":null,\"backlight\":null,"
        "\"spi_mode\":0,\"spi_hz\":8000000,\"profile\":\"esp_hi_st7735_bsp\",\"controller_verified\":false,"
        "\"source\":\"xiaozhi ac6deed3 BSP\",\"gpio_owner\":5},"
        "\"body_gpio\":[20,21],\"usb_reserved\":[18,19],\"flash_reserved\":[12,13,14,15,16,17],"
        "\"unverified_reserved\":[11],\"pcb_revision\":null}");
    return w.error;
}
