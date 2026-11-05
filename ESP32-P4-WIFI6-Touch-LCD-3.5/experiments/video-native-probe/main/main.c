#include <stdio.h>
#include <string.h>
#include "esp_h264_dec_sw.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_private/esp_clk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define SAMPLE(id, file) extern const unsigned char id##_start[] asm("_binary_" file "_h264_start"); extern const unsigned char id##_end[] asm("_binary_" file "_h264_end")
SAMPLE(high,"wan3_high_head");
SAMPLE(small,"320x480");
SAMPLE(medium,"480x864");
SAMPLE(large,"720x1280");
static void test(const char *name,const unsigned char *start,const unsigned char *end){
 size_t length=end-start;uint32_t before=esp_get_free_heap_size();
 uint8_t *data=heap_caps_malloc(length,MALLOC_CAP_SPIRAM);if(!data)return;memcpy(data,start,length);
 esp_h264_dec_cfg_sw_t cfg={.pic_type=ESP_H264_RAW_FMT_I420};esp_h264_dec_handle_t decoder=NULL;
 int error=esp_h264_dec_sw_new(&cfg,&decoder);if(!error)error=esp_h264_dec_open(decoder);
 esp_h264_dec_in_frame_t in={.raw_data={.buffer=data,.len=length}};esp_h264_dec_out_frame_t out={0};
 unsigned frames=0;uint32_t minimum=esp_get_free_heap_size();int64_t elapsed=0,max_frame=0;
 while(!error&&in.raw_data.len){
  int64_t began=esp_timer_get_time();error=esp_h264_dec_process(decoder,&in,&out);int64_t us=esp_timer_get_time()-began;
  elapsed+=us;if(us>max_frame)max_frame=us;
  if(out.out_size)++frames;
  uint32_t free=esp_get_free_heap_size();if(free<minimum)minimum=free;
  if(error||!in.consume)break;
  in.raw_data.buffer+=in.consume;in.raw_data.len-=in.consume;
  vTaskDelay(1);
 }
 if(decoder){esp_h264_dec_close(decoder);esp_h264_dec_del(decoder);}free(data);
 printf("@video_probe {\"sample\":\"%s\",\"cpu_mhz\":%d,\"frames\":%u,\"decode_us\":%lld,\"decode_fps\":%.2f,\"max_call_us\":%lld,\"heap_delta_bytes\":%lu,\"heap_after\":%lu,\"error\":%d,\"remaining\":%lu,\"audio_running\":false,\"display_running\":false}\n",name,esp_clk_cpu_freq()/1000000,frames,(long long)elapsed,elapsed?frames*1000000.0/elapsed:0.0,(long long)max_frame,(unsigned long)(before-minimum),(unsigned long)esp_get_free_heap_size(),error,(unsigned long)in.raw_data.len);
}
void app_main(void){
 esp_pm_config_t pm={.max_freq_mhz=200,.min_freq_mhz=200,.light_sleep_enable=false};ESP_ERROR_CHECK(esp_pm_configure(&pm));
 vTaskDelay(pdMS_TO_TICKS(5000));
 test("WAN3 original High profile 8 copied access units",high_start,high_end);
 test("baseline 320x480 fixture only",small_start,small_end);
 test("baseline 480x864 fixture only",medium_start,medium_end);
 test("baseline 720x1280 fixture only",large_start,large_end);
 puts("@video_probe_done");
}
