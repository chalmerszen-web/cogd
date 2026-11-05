#include "control_board.h"
#include "audio_board.h"
#include "display_board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
agent_control_t esp_hi_control;
agent_devices_t esp_hi_devices;
static agent_resources_t resources;
static TaskHandle_t task;
static void run(void *p)
{
    (void)p;
    for(;;) {
        agent_devices_poll(&esp_hi_devices);
        agent_control_tick(&esp_hi_control,esp_timer_get_time()/1000);
        esp_hi_display_tick(esp_timer_get_time()/1000);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
unsigned esp_hi_control_stack(void) {return task?(unsigned)uxTaskGetStackHighWaterMark(task):0;}
agent_err_t esp_hi_display_pins(bool claim,bool *held)
{if(held)*held=claim;return AGENT_OK;}
agent_err_t esp_hi_storage_begin(void)
{
    uint32_t added;
    return agent_resources_claim(&resources,AGENT_OWNER_STORAGE,AGENT_RES_MIC|AGENT_RES_SPEAKER,&added);
}
void esp_hi_storage_end(void)
{while(agent_resources_release(&resources,AGENT_OWNER_STORAGE,AGENT_RES_MIC|AGENT_RES_SPEAKER)==AGENT_ERR_BUSY)vTaskDelay(1);}
agent_err_t esp_hi_control_init(void)
{
    agent_resources_init(&resources);
    /* No generic GPIO tools until the future towel-machine IO contract exists. */
    agent_control_backend_t backend={.resources=&resources,.audio=&esp_hi_audio_ops,
        .speaker_mask=AGENT_RES_SPEAKER,.mic_mask=AGENT_RES_MIC};
    agent_control_init(&esp_hi_control,&backend);
    agent_devices_init(&esp_hi_devices,&backend,&esp_hi_control);
    agent_err_t e=esp_hi_display_init();if(e)return e;
    return xTaskCreate(run,"p4_control",4096,NULL,4,&task)==pdPASS?AGENT_OK:AGENT_ERR_MEMORY;
}
