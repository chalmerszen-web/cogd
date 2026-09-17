#include "control_board.h"
#include "board.h"
#include "audio_board.h"
#include "display_board.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

agent_control_t esp_hi_control;
agent_devices_t esp_hi_devices;
static agent_resources_t resources;
static TaskHandle_t control_task;
/* LCD pins remain available only while its driver does not hold their group. */
#define GPIO_IO (AGENT_GPIO_READ|AGENT_GPIO_WRITE|AGENT_GPIO_PWM)
static const agent_pin_t pins[]={
    {0,AGENT_GPIO_READ,"button_top"},{1,AGENT_GPIO_READ,"button_bottom"},{9,AGENT_GPIO_READ,"boot_button"},
    {4,GPIO_IO,"lcd_gpio4"},{5,GPIO_IO,"lcd_gpio5"},{10,GPIO_IO,"lcd_gpio10"},
    {20,GPIO_IO,"body_gpio20"},{21,GPIO_IO,"body_gpio21"}
};
static agent_pin_state_t state[22];
static int pwm_pin[2]={-1,-1};
static unsigned modes(unsigned pin)
{ for(size_t i=0;i<sizeof(pins)/sizeof(*pins);++i) if(pins[i].pin==pin) return pins[i].modes; return 0; }
static agent_err_t pin_get(void *ctx,unsigned pin,agent_pin_state_t *out)
{
    (void)ctx; if(!out || pin>21 || !modes(pin)) return AGENT_ERR_FORBIDDEN;
    *out=state[pin]; out->value=(unsigned)gpio_get_level((gpio_num_t)pin);
    if(out->mode==AGENT_GPIO_PWM) for(unsigned i=0;i<2;++i) if(pwm_pin[i]==(int)pin) {
        out->hz=ledc_get_freq(LEDC_LOW_SPEED_MODE,(ledc_timer_t)i);
        out->duty=(ledc_get_duty(LEDC_LOW_SPEED_MODE,(ledc_channel_t)i)*1000+511)/1023;
    }
    return AGENT_OK;
}
static agent_err_t pin_set(void *ctx,unsigned pin,const agent_pin_state_t *next)
{
    (void)ctx;
    if(!next || pin>21 || !(modes(pin)&next->mode)) return AGENT_ERR_FORBIDDEN;
    if(next->mode==AGENT_GPIO_PWM && (next->hz<10 || next->hz>5000 || next->duty>1000)) return AGENT_ERR_ARGUMENT;
    if(next->mode==AGENT_GPIO_WRITE && next->value>1) return AGENT_ERR_ARGUMENT;
    esp_err_t error=ESP_OK;
    if(next->mode==AGENT_GPIO_PWM) {
        int channel=-1;
        for(unsigned i=0;i<2;++i) if(pwm_pin[i]==(int)pin) channel=(int)i;
        if(channel<0) for(unsigned i=0;i<2;++i) if(pwm_pin[i]<0) { channel=(int)i; break; }
        if(channel<0) return AGENT_ERR_BUSY;
        ledc_timer_config_t timer={.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_10_BIT,
            .timer_num=(ledc_timer_t)channel,.freq_hz=next->hz,.clk_cfg=LEDC_AUTO_CLK};
        error=ledc_timer_config(&timer);
        if(!error) {
            ledc_channel_config_t config={.gpio_num=(int)pin,.speed_mode=LEDC_LOW_SPEED_MODE,
                .channel=(ledc_channel_t)channel,.timer_sel=(ledc_timer_t)channel,.duty=next->duty*1023/1000};
            error=ledc_channel_config(&config);
            if(!error) {
                pwm_pin[channel]=(int)pin;
                error=gpio_input_enable((gpio_num_t)pin);
            }
        }
    } else if(next->mode==AGENT_GPIO_READ || next->mode==AGENT_GPIO_WRITE) {
        for(unsigned i=0;i<2;++i) if(pwm_pin[i]==(int)pin) {
            error=ledc_stop(LEDC_LOW_SPEED_MODE,(ledc_channel_t)i,0); if(error) return AGENT_ERR_TOOL;
            pwm_pin[i]=-1;
        }
        error=gpio_reset_pin((gpio_num_t)pin);
        if(!error && next->mode==AGENT_GPIO_WRITE) error=gpio_set_level((gpio_num_t)pin,next->value);
        gpio_config_t config={.pin_bit_mask=1ULL<<pin,
            .mode=next->mode==AGENT_GPIO_READ?GPIO_MODE_INPUT:GPIO_MODE_INPUT_OUTPUT,
            .pull_up_en=next->mode==AGENT_GPIO_READ && modes(pin)==AGENT_GPIO_READ?GPIO_PULLUP_ENABLE:GPIO_PULLUP_DISABLE};
        if(!error) error=gpio_config(&config);
    } else return AGENT_ERR_ARGUMENT;
    if(!error) state[pin]=*next;
    return error?AGENT_ERR_TOOL:AGENT_OK;
}
static void run(void *arg)
{
    (void)arg;
    for(;;) {
        agent_devices_poll(&esp_hi_devices);
        agent_control_tick(&esp_hi_control,(uint64_t)esp_timer_get_time()/1000);
        esp_hi_display_tick((uint64_t)esp_timer_get_time()/1000);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
unsigned esp_hi_control_stack(void) { return control_task?(unsigned)uxTaskGetStackHighWaterMark(control_task):0; }
agent_err_t esp_hi_display_pins(bool claim,bool *held)
{
    const uint32_t mask=AGENT_PIN(4)|AGENT_PIN(5)|AGENT_PIN(10);
    agent_control_t *c=&esp_hi_control;
    if(atomic_flag_test_and_set(&c->admission)) return AGENT_ERR_BUSY;
    agent_err_t e=AGENT_OK;uint32_t added=0;
    if(claim && atomic_load(&c->active)) {e=AGENT_ERR_BUSY;goto done;}
    if(claim) {e=agent_resources_claim(&resources,AGENT_OWNER_DISPLAY,mask,&added);if(e) goto done;*held=true;}
    const unsigned lcd_pins[]={4,5,10};agent_pin_state_t input={.mode=AGENT_GPIO_READ};
    for(unsigned i=0;i<3 && !e;++i) e=pin_set(NULL,lcd_pins[i],&input);
    if(!claim || e) {
        agent_err_t released=agent_resources_release(&resources,AGENT_OWNER_DISPLAY,mask);
        if(!released) *held=false;
        if(!e) e=released;
    }
done:
    atomic_flag_clear(&c->admission);return e;
}
agent_err_t esp_hi_storage_begin(void)
{
    if(!control_task) return AGENT_OK;
    uint32_t added;
    const agent_control_backend_t *b=&esp_hi_control.backend;
    return agent_resources_claim(&resources,AGENT_OWNER_STORAGE,b->speaker_mask|b->mic_mask,&added);
}
void esp_hi_storage_end(void)
{
    if(!control_task) return;
    const agent_control_backend_t *b=&esp_hi_control.backend;
    while(agent_resources_release(&resources,AGENT_OWNER_STORAGE,b->speaker_mask|b->mic_mask)==AGENT_ERR_BUSY)
        vTaskDelay(1);
}
agent_err_t esp_hi_control_init(void)
{
    if(control_task) return AGENT_OK;
    agent_resources_init(&resources);
    agent_control_backend_t backend={.resources=&resources,.pins=pins,.pin_count=sizeof(pins)/sizeof(*pins),
        .light_mask=AGENT_RES_LIGHT|AGENT_PIN(8),.light_get=esp_hi_light_get,.light_set=esp_hi_light_set,
        .pin_get=pin_get,.pin_set=pin_set};
    uint32_t allowed=AGENT_PIN(8);
    for(size_t i=0;i<sizeof(pins)/sizeof(*pins);++i) allowed|=AGENT_PIN(pins[i].pin);
#if AGENT_ENABLE_AUDIO
    backend.audio=&esp_hi_audio_ops;
    backend.speaker_mask=AGENT_RES_SPEAKER|AGENT_PIN(3)|AGENT_PIN(6)|AGENT_PIN(7);
    backend.mic_mask=AGENT_RES_MIC|AGENT_PIN(2);
    allowed|=AGENT_PIN(2)|AGENT_PIN(3)|AGENT_PIN(6)|AGENT_PIN(7);
#endif
    backend.reserved_mask=((AGENT_PIN(22)-1)&~allowed);
    uint32_t added;
    agent_err_t error=agent_resources_claim(&resources,AGENT_OWNER_SYSTEM,backend.reserved_mask,&added);
    if(error) return error;
    for(size_t i=0;i<sizeof(pins)/sizeof(*pins);++i) {
        agent_pin_state_t input={.mode=AGENT_GPIO_READ};
        error=pin_set(NULL,pins[i].pin,&input); if(error) return error;
    }
    agent_control_init(&esp_hi_control,&backend);
    agent_devices_init(&esp_hi_devices,&backend,&esp_hi_control);
    error=esp_hi_display_init();if(error) return error;
    return xTaskCreate(run,"agent_control",2048,NULL,4,&control_task)==pdPASS?AGENT_OK:AGENT_ERR_MEMORY;
}
