/* Recorded-PCM inference probe only. No microphone, network, dialogue or NVS writes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"
#include "cJSON.h"

extern const uint8_t models_start[] asm("_binary_models_bin_start");
extern const uint8_t models_end[] asm("_binary_models_bin_end");
static const struct { int id; const char *phrase; } commands[]={
    {1,"ni hao xiao yan"},{2,"hai xiao yan"},
    {3,"nei hou xiu yin"},{4,"hai xiu yin"},
    {3,"nei hou xiu yan"},{4,"hai xiu yan"}
};

static void send_json(cJSON *value)
{
    char *text=cJSON_PrintUnformatted(value);
    if(text){printf("%s\n",text);fflush(stdout);free(text);}
    cJSON_Delete(value);
}

static int read_exact(void *buffer,size_t bytes)
{
    size_t offset=0;unsigned idle=0;
    while(offset<bytes) {
        int n=uart_read_bytes(UART_NUM_0,(uint8_t *)buffer+offset,bytes-offset,pdMS_TO_TICKS(1000));
        if(n<0 || (!n && ++idle>30))return -1;
        if(n>0){offset+=(size_t)n;idle=0;}
    }
    return 0;
}

static uint32_t pcm_hash(const void *data,size_t bytes)
{
    const uint8_t *p=data;uint32_t hash=2166136261u;
    for(size_t i=0;i<bytes;++i)hash=(hash^p[i])*16777619u;
    return hash;
}

void app_main(void)
{
    const esp_pm_config_t cpu={.max_freq_mhz=200,.min_freq_mhz=200,.light_sleep_enable=false};
    ESP_ERROR_CHECK(esp_pm_configure(&cpu));
    const uart_config_t config={.baud_rate=921600,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,
        .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0,&config));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0,8192,0,0,NULL,0));
    srmodel_list_t *list=srmodel_load(models_start);
    char *name=list?esp_srmodel_filter(list,"mn7","cn"):NULL;
    if(!name){printf("{\"type\":\"fatal\",\"reason\":\"models\"}\n");return;}
    esp_mn_iface_t *model=esp_mn_handle_from_name(name);
    if(!model){printf("{\"type\":\"fatal\",\"reason\":\"interface\"}\n");return;}
    printf("{\"type\":\"ready\",\"model\":\"%s\",\"model_blob_bytes\":%u,\"cpu_mhz\":200}\n",name,(unsigned)(models_end-models_start));fflush(stdout);
    for(;;) {
        char line[96];size_t used=0;
        do {
            if(read_exact(line+used,1)) { used=0;continue; }
            if(line[used]=='\n')break;
            if(++used>=sizeof(line)-1)used=0;
        } while(1);
        line[used]=0;
        if(!strcmp(line,"PING")){printf("{\"type\":\"pong\",\"cpu_mhz\":200}\n");fflush(stdout);continue;}
        unsigned frames=0;float threshold=0;
        if(sscanf(line,"BEGIN %u %f",&frames,&threshold)!=2 || frames<1 || frames>3000 || threshold<.05f || threshold>.95f){printf("{\"type\":\"error\",\"reason\":\"command\"}\n");fflush(stdout);continue;}
        size_t internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),external=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        int64_t start=esp_timer_get_time();
        model_iface_data_t *state=model->create(name,12000);
        if(!state){printf("{\"type\":\"fatal\",\"reason\":\"create\"}\n");fflush(stdout);return;}
        /* MultiNet7 P4 leaves this optional interface member NULL. */
        if(model->switch_loader_mode)state=model->switch_loader_mode(state,ESP_MN_LOAD_FROM_PSRAM);
        if(!state){printf("{\"type\":\"fatal\",\"reason\":\"loader\"}\n");fflush(stdout);return;}
        ESP_ERROR_CHECK(esp_mn_commands_alloc(model,state));
        for(unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);++i)ESP_ERROR_CHECK(esp_mn_commands_add(commands[i].id,commands[i].phrase));
        esp_mn_error_t *errors=esp_mn_commands_update();
        if(errors && errors->num){printf("{\"type\":\"fatal\",\"reason\":\"phrases\",\"count\":%d}\n",errors->num);fflush(stdout);return;}
        model->set_det_threshold(state,threshold);
        int chunk=model->get_samp_chunksize(state),rate=model->get_samp_rate(state);
        if(chunk<=0 || chunk>2048 || rate!=16000){printf("{\"type\":\"fatal\",\"reason\":\"format\"}\n");fflush(stdout);return;}
        int16_t *pcm=heap_caps_malloc((size_t)chunk*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        if(!pcm)abort();
        printf("{\"type\":\"begin\",\"frames\":%u,\"chunk\":%d,\"threshold\":%.4f,\"internal_heap\":%u,\"psram_heap\":%u,\"create_us\":%lld}\n",frames,chunk,threshold,
            (unsigned)(internal-heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),(unsigned)(external-heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),(long long)(esp_timer_get_time()-start));fflush(stdout);
        unsigned peak=0;uint64_t sum=0;int failed=0;
        for(unsigned frame=0;frame<frames;++frame) {
            if(read_exact(pcm,(size_t)chunk*2)){failed=1;break;}
            start=esp_timer_get_time();esp_mn_state_t result=model->detect(state,pcm);unsigned elapsed=(unsigned)(esp_timer_get_time()-start);
            if(elapsed>peak)peak=elapsed;
            sum+=elapsed;
            cJSON *row=cJSON_CreateObject();cJSON_AddStringToObject(row,"type","frame");cJSON_AddNumberToObject(row,"frame",frame);cJSON_AddNumberToObject(row,"us",elapsed);cJSON_AddNumberToObject(row,"state",result);
            cJSON_AddNumberToObject(row,"pcm_fnv1a",pcm_hash(pcm,(size_t)chunk*2));
            if(result==ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *r=model->get_results(state);cJSON *matches=cJSON_AddArrayToObject(row,"matches");
                if(r) {
                    cJSON_AddStringToObject(row,"raw",r->raw_string);
                    for(int i=0;i<r->num && i<ESP_MN_RESULT_MAX_NUM;++i){cJSON *match=cJSON_CreateObject();cJSON_AddNumberToObject(match,"id",r->command_id[i]);cJSON_AddNumberToObject(match,"prob",r->prob[i]);cJSON_AddItemToArray(matches,match);}
                }
                model->clean(state);
            } else if(result==ESP_MN_STATE_TIMEOUT)model->clean(state);
            send_json(row);
        }
        free(pcm);model->destroy(state);
        printf("{\"type\":\"end\",\"failed\":%d,\"max_us\":%u,\"mean_us\":%.2f}\n",failed,peak,(double)sum/frames);fflush(stdout);
    }
}
