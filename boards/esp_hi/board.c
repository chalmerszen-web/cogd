#include "board.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if !CONFIG_IDF_TARGET_ESP32C3
#error "This board adapter requires ESP32-C3."
#endif

enum { RGB_GPIO = 8, RGB_COUNT = 4 };
static led_strip_handle_t strip;
static SemaphoreHandle_t light_lock;
static atomic_uint current_rgb;

agent_err_t esp_hi_light_init(void)
{
    light_lock = xSemaphoreCreateMutex();
    if (!light_lock) return AGENT_ERR_MEMORY;
    led_strip_config_t config = { .strip_gpio_num = RGB_GPIO, .max_leds = RGB_COUNT,
        .led_model = LED_MODEL_WS2812, .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB };
    led_strip_rmt_config_t rmt = { .resolution_hz = 10000000, .mem_block_symbols = 64 };
    if (led_strip_new_rmt_device(&config, &rmt, &strip) != ESP_OK) return AGENT_ERR_TOOL;
    const uint8_t off[3] = {0};
    return esp_hi_light_set(NULL, off);
}

agent_err_t esp_hi_light_get(void *ctx, uint8_t rgb[3])
{
    (void)ctx;
    unsigned packed = atomic_load(&current_rgb);
    rgb[0] = (uint8_t)(packed >> 16); rgb[1] = (uint8_t)(packed >> 8); rgb[2] = (uint8_t)packed;
    return strip ? AGENT_OK : AGENT_ERR_TOOL;
}

agent_err_t esp_hi_light_set(void *ctx, const uint8_t rgb[3])
{
    (void)ctx;
    if (!strip || xSemaphoreTake(light_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return AGENT_ERR_TOOL;
    esp_err_t error = ESP_OK;
    for (unsigned i = 0; i < RGB_COUNT && error == ESP_OK; ++i)
        error = led_strip_set_pixel(strip, i, rgb[0], rgb[1], rgb[2]);
    if (error == ESP_OK) error = led_strip_refresh(strip);
    if (error == ESP_OK) atomic_store(&current_rgb, ((unsigned)rgb[0] << 16) | ((unsigned)rgb[1] << 8) | rgb[2]);
    xSemaphoreGive(light_lock);
    return error == ESP_OK ? AGENT_OK : AGENT_ERR_TOOL;
}
