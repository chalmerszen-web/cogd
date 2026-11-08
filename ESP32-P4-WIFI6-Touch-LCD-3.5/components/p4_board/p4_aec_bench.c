#include "p4_aec_bench.h"
#include "esp_aec.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_clk.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned frames,linear_max,nlp_max,frame_max,overruns,cpu_mhz;
    uint64_t linear_sum,nlp_sum,cycles;
    int error;
} result_t;
typedef struct {SemaphoreHandle_t done;result_t rows[4];} work_t;

static void benchmark(void *arg)
{
    work_t *work=arg;
    int16_t *pcm=heap_caps_aligned_alloc(16,3*512*sizeof(int16_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    for(unsigned test=0;test<4;test++) {
        result_t *r=&work->rows[test];r->cpu_mhz=esp_clk_cpu_freq()/1000000;
        if(!pcm){r->error=19;continue;}
        aec_config_t cfg={.mic_num=1,.ref_num=1,.out_num=1,.filter_length=4,.sample_rate=16000,
            .caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT,.mode=(test&1)?AEC_MODE_FD_LOW_COST:AEC_MODE_FD_HIGH_PERF,
            .nlp_level=AEC_NLP_LEVEL_NORMAL};
        if(test>=2)cfg.caps|=MALLOC_CAP_CACHE_ALIGNED;
        aec_handle_t *aec=aec_create_from_config(&cfg);
        if(!aec){r->error=19;continue;}
        if(aec_get_chunksize(aec)!=512){r->error=1;aec_destroy(aec);continue;}
        uint32_t rng=0x19542763;int previous=0;
        int64_t deadline=esp_timer_get_time()+8000000;
        for(unsigned frame=0;frame<48;frame++) {
            for(unsigned i=0;i<512;i++) {
                rng=rng*1664525u+1013904223u;
                int reference=((int)(rng>>16)-32768)/4;
                pcm[512+i]=(int16_t)reference;
                pcm[i]=(int16_t)(previous/2+((int)(rng&65535)-32768)/16);
                previous=reference;
            }
            uint32_t cycle=esp_cpu_get_cycle_count();int64_t begin=esp_timer_get_time();
            aec_linear_process(aec,pcm,pcm+512,pcm+1024);
            int64_t linear=esp_timer_get_time();aec_nlp_process(aec,pcm+1024);
            int64_t end=esp_timer_get_time();unsigned a=(unsigned)(linear-begin),b=(unsigned)(end-linear),total=(unsigned)(end-begin);
            r->cycles+=(uint32_t)(esp_cpu_get_cycle_count()-cycle);
            ++r->frames;r->linear_sum+=a;r->nlp_sum+=b;
            if(a>r->linear_max)r->linear_max=a;
            if(b>r->nlp_max)r->nlp_max=b;
            if(total>r->frame_max)r->frame_max=total;
            if(total>32000)++r->overruns;
            /* Yield even when a bad profile exceeds its real-time budget. */
            vTaskDelay(1);
            if(end>=deadline){r->error=4;break;}
        }
        aec_destroy(aec);
    }
    heap_caps_free(pcm);xSemaphoreGive(work->done);vTaskDelete(NULL);
}
int p4_aec_bench(char *out,size_t cap)
{
    work_t work={0};work.done=xSemaphoreCreateBinary();if(!work.done)return 19;
    if(xTaskCreatePinnedToCore(benchmark,"aec_bench",8192,&work,19,NULL,1)!=pdPASS){vSemaphoreDelete(work.done);return 19;}
    /* The worker owns no network or user data, and each profile is bounded. */
    xSemaphoreTake(work.done,portMAX_DELAY);vSemaphoreDelete(work.done);
    int used=snprintf(out,cap,"{\"aec_offline_benchmark\":true,\"synthetic\":true,\"core\":1,\"rows\":[");
    for(unsigned i=0;i<4;i++) {
        result_t *r=&work.rows[i];
        used+=snprintf(out+used,cap-used,"%s{\"mode\":%u,\"cache_aligned\":%s,\"frames\":%u,\"cpu_mhz\":%u,\"linear_sum_us\":%llu,\"nlp_sum_us\":%llu,\"linear_max_us\":%u,\"nlp_max_us\":%u,\"frame_max_us\":%u,\"over_32ms\":%u,\"cycles\":%llu,\"error\":%d}",
            i?",":"",(i&1)?5u:6u,i>=2?"true":"false",r->frames,r->cpu_mhz,
            (unsigned long long)r->linear_sum,(unsigned long long)r->nlp_sum,r->linear_max,r->nlp_max,r->frame_max,r->overruns,(unsigned long long)r->cycles,r->error);
    }
    snprintf(out+used,cap-used,"]}");return 0;
}

int p4_aec_tasks(char *out,size_t cap)
{
#if configGENERATE_RUN_TIME_STATS && configUSE_TRACE_FACILITY
    /* Diagnostic builds only. Avoid uxTaskGetSystemState's stack scans on
     * the live audio path. These are persistent application/driver tasks. */
    static const char *const names[]={"p4_mic","agent_audio","agent_net","agent_control",
        "sdio_rx_buf","sdio_read","sdio_process_rx","sdio_write","rpc_rx","rpc_tx",
        "tcpip_thread","tiT","lvgl_task","lvgl","IDLE0","IDLE1","esp_timer"};
    if(cap<4096)return 3;
    int used=snprintf(out,cap,"{\"aec_tasks\":true,\"at_us\":%llu,\"cpu_mhz\":%u,\"rows\":[",
        (unsigned long long)esp_timer_get_time(),(unsigned)(esp_clk_cpu_freq()/1000000));
    unsigned emitted=0;
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {
        TaskHandle_t task=xTaskGetHandle(names[i]);if(!task)continue;
        TaskStatus_t status;vTaskGetInfo(task,&status,pdFALSE,eRunning);
        used+=snprintf(out+used,cap-used,"%s{\"name\":\"%s\",\"id\":%u,\"priority\":%u,\"base_priority\":%u,\"core\":%d,\"runtime_us\":%llu}",
            emitted++?",":"",names[i],(unsigned)status.xTaskNumber,(unsigned)status.uxCurrentPriority,
            (unsigned)status.uxBasePriority,(int)xTaskGetCoreID(task),
            (unsigned long long)status.ulRunTimeCounter);
    }
    snprintf(out+used,cap-used,"]}");return 0;
#else
    (void)out;(void)cap;return 3;
#endif
}
