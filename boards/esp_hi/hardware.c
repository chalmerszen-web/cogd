#include "hardware.h"
#include "json.h"

const char esp_hi_hardware_prompt[]=
    "Authoritative current ESP-HI head hardware, superseding older dialogue about missing LCD support: "
    "ESP32-C3 rev0.4,4MiB Flash. GPIO0=MOVE_WAKE,1=AUDIO_WAKE,9=BOOT button (read only inputs); "
    "2=ADC1_CH2 microphone;3=PA enable;6/7=PDM speaker;8=four WS2812 RGB LEDs. "
    "LCD=160x80 SPI2: MOSI4,SCLK5,DC10,mode0; CS/RST/MISO/backlight have no controllable pin. "
    "Use the firmware's esp_hi_st7735_bsp panel profile; exact physical chip is not read back. "
    "Use device_display_set(mode=clock,utc_offset_minutes=480) for a persistent local Beijing clock; "
    "set once, the device refreshes every second without more LLM calls. Off releases LCD pins as inputs. "
    "Do not bit-bang the display via GPIO tools. Query display status: frames indicate SPI writes, not visual confirmation. "
    "Clock time comes from device NTP; never invent the current time. Unset time shows --:--:--. "
    "Display mode/ready/frames describe software transport only, never proof that pixels are visible. "
    "GPIO4/5/10 remain general GPIO only while LCD is off; owner5 means display. "
    "20/21 are body-interface outputs, not verified unused pads; no servo driver. "
    "18/19 are active USB,12..17 Flash,11 unverified: never repurpose them. "
    "Use dedicated audio/LED tools for managed hardware; device_hardware_get reports wiring and availability."
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
